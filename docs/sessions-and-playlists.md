# Sessions and playlists

A session is a `*.session.json` file — plain JSON, spec in
[session-json-format.md](session-json-format.md) (normative), loaded by
`src/common/session_json.cpp`. It holds everything a session needs except the
media files themselves (which it references by relative path). In memory it
becomes a `trance_pb::Session` — the protobuf (`src/common/trance.proto`) is the
**frozen in-memory model only**; legacy protobuf `.session` files are
auto-migrated to JSON on load. This document tours the
schema, the load path, theme shuffling, and — in detail — the playlist "VM" that
the README mentions but doesn't explain.

## Schema tour (the in-memory model)

The in-memory schema lives in `src/common/trance.proto`; the JSON on-disk shape
mirrors it (see [session-json-format.md](session-json-format.md) for the exact
key-by-key mapping). The four headline concepts:

- **`Theme`** (`:255`) — a bag of media: `image_path`, `animation_path`,
  `font_path`, and `text_line`s. Paths are relative to the session's root
  directory.
- **`Program`** (`:126`) — a selection of themes (`enabled_theme`, each with a
  `random_weight`; one may be `pinned` to always be active) plus display settings:
  visual-type weights (`visual_type`), `global_fps`, `zoom_intensity`, spiral
  colours, text colours, the `entrainment` bed, and authorable
  `custom_visual_pattern`s.
- **`PlaylistItem`** (`:217`) — one node in the playlist graph. A `oneof` makes it
  either a `Standard` item (play a program for `play_time_seconds`) or a
  `Subroutine` (a list of other item names to run in order). It carries
  `next_item` branches and `audio_event`s.
- **`Session`** (`:275`) — the top level: `first_playlist_item` (the entry node),
  the `playlist` map (name → `PlaylistItem`), `program_map`, `theme_map`, and
  `variable_map`.

Key sub-messages:

- **`VisualType`** enum (`:128`) — the eight built-in visuals (`ACCELERATE` …
  `SUPER_FAST`); `VisualTypeConfig` pairs a type with its selection weight.
- **`VisualPatternSource`** (`:115`) — an authorable custom pattern stored as v3
  `source_text` with a `name`, `random_weight`, and `enabled` flag. See
  [authoring-v3-patterns.md](authoring-v3-patterns.md).
- **`Entrainment`** / **`EntrainmentLayer`** (`:88`, `:105`) — the synthesised
  audio bed. See [audio.md](audio.md).
- **`AudioEvent`** (`:193`) — a music-channel cue attached to a playlist item.
- **`Variable`** (`:266`) — a named session variable with allowed `value`s and a
  `default_value`; referenced by playlist conditionals.
- **`PlaylistItem.NextItem`** (`:238`) — one weighted, optionally-conditional
  branch out of a playlist item (the heart of the VM, below).

The proto also carries a `SessionArchive` message (a session plus an embedded file
table). It is **dead schema** — the shipped bundle format is a plain zip, not this.
`--export_archive out.trance` (`export_session_archive`,
`src/common/session_archive.{h,cpp}`) writes the session JSON as `session.json` at the
zip root plus every asset it references under its existing root-relative path. There is
deliberately no importer: any zip tool extracts a `.trance` straight back into a valid
session root.

## Load path

`main()` (`src/trance/main.cpp`) loads the session:

1. `load_session(path)` (`src/common/session.cpp`) takes a `*.session.json`
   path (a legacy `.session` proto path is transparently migrated to a JSON
   sibling first — session-json-format.md §7), parses it via `load_session_json()`
   (`src/common/session_json.cpp`), then runs `validate_session()`.
2. `validate_session()` (`session.cpp`) is the repair pass: it fills empty
   maps with defaults, migrates deprecated fields (`enabled_theme_name` →
   `enabled_theme`, the old flat `program`/`play_time_seconds` →
   `Standard`), clamps numeric ranges, prunes dangling subroutine references and
   conditional variables, and repoints `first_playlist_item` if it names a missing
   item. A program with no usable themes or no visual weights is given defaults
   (`validate_program`).
3. On a load failure, `main()` falls back to `get_default_session()` plus
   `search_resources()`, which walks the directory next to the binary and builds one
   theme per directory that DIRECTLY contains at least one file, at any depth, named by
   its root-relative path (`hypno`, `hypno/spam`). Loose files at the root become the
   reserved `/root/` theme. Nothing is merged across themes — the old `/wildcards/`
   merge is retired. That is how `trance.exe` runs against a bare folder of images with
   no session file.

The session's root directory is the parent of the session path; all media paths
resolve against it.

## Theme bank and shuffling

`ThemeBank` (`src/trance/theme_bank.{h,cpp}`) supplies images/animations/text/fonts
to the visual engine. Its scheme (class comment, `theme_bank.h:26`): keep **two
themes active** in memory at all times, and asynchronously load a **third** so the
active set can swap without a stall. The active set is a 4-slot queue —
`[0]` unloading, `[1]` primary (`get_image(false)`), `[2]` alternate
(`get_image(true)`), `[3]` loading-next (`theme_bank.h:71`).

Selection is weighted-random via `Shuffler` (one per theme for image loading,
image picking, animations, and text lines; `ThemeInfo`, `theme_bank.h`). The
program's `enabled_theme` weights pick which themes become active; a `pinned`
theme is always one of the two active, with the weights choosing only the other.
The frame loop calls `change_themes()` when `swaps_to_match_theme()` reports a
pending swap; `set_program` reweights the shuffler when the playlist switches
programs.

Within a single theme that inherits from an ancestor (`scan.inherit`), images are
**tier-weighted, not drawn flat**: the pool is split into per-tier shufflers (tier 0
is the theme's own files, then each ancestor in chain order) and `weighted_tier()`
picks the tier by its source theme's `random_weight` before picking an image inside
it. Both the draw path and the *load* path use the same weighting — that pairing is
load-bearing, because weighting only the draw against a tier-blind cache made
residency track tier size while selection tracked tier weight. A theme with fewer
than two surviving tiers collapses to a single flat shuffle. Animations, text and
audio still draw flat from the merged pool; only images are tiered. Full rules:
[session-json-format.md](session-json-format.md) §3.2.

## The playlist VM

The playlist is a directed graph of `PlaylistItem`s executed by `PlaylistRunner`
(`src/trance/playlist_runner.{h,cpp}`) — a small stack machine extracted from
`play_session()` so the transition logic is testable (`tests/playlist_runner_test.cpp`).
The state is a `std::vector<Entry>` where each entry is `{item, subroutine_step}`;
construction pushes `first_playlist_item`. The driver is the `while (true)` loop in
`advance()`.

The split of responsibility is the thing to remember: **the runner owns the stack and
the switch clock; the caller owns every side effect.** `play_session()` feeds it a
wall-clock timestamp in milliseconds and gets an `on_enter` callback per newly-entered
item, which is where audio events, program switches and theme reweighting happen.
A paused frame calls `freeze(elapsed_ms)`, shifting the switch clock forward so a held
item doesn't time out behind frozen visuals.

### Standard items and timing

A `Standard` item plays its `program` for `play_time_seconds`. `advance()` compares
elapsed wall-clock since the last switch; while a standard item's time hasn't elapsed
it breaks out of the loop and keeps playing. When the time is up it follows a branch.

### Branching (`next_item`)

Each item has a list of `NextItem` branches (`trance.proto:238`). The next item is
chosen by **weighted random** over the *enabled* branches —
`next_playlist_item()` (`playlist_runner.cpp`) sums `random_weight` across branches
that pass their condition, rolls `random(total)`, and returns the matching branch's
`playlist_item_name`. A branch with total weight 0 (or no branches) ends the
current item.

### Conditionals (`is_enabled`)

A branch can gate on a session variable. `NextItem.condition_variable_name` /
`condition_variable_value` reference an entry in `variable_map`;
`is_enabled(next, variables)` (`src/common/session.cpp`) returns true when the
variable has no condition, or when the live variable's value equals the required
value. Branches that fail the condition are dropped from the weighted pick — so
variables steer which paths through the playlist are reachable.

Variable values come from the `--variables` command-line flag, a
semicolon-separated `key=value` list parsed by `parse_variables` (`main.cpp`, with
`\` escaping for `;`, `=`, `\`) and handed to the runner at construction. The last
values per session persist in `System.last_session_map` (`trance.proto:76`).

### Subroutines

A `Subroutine` item is a list of other item names (`trance.proto:225`). When the
loop reaches a subroutine entry it **pushes** the named child onto the stack and
advances the parent's `subroutine_step`. Each pushed child runs as its own item
(standard or another subroutine — they nest). When a child has no enabled
`next_item` and the stack is deeper than one, the loop **pops** back to the parent
and continues to the parent's next step. The stack is bounded by `MAXIMUM_STACK`
(256, `common/common.h`); overflow is reported and the subroutine is abandoned
rather than recursing forever.

Each entry fires the runner's `on_enter` callback, and `play_session()`'s
implementation of it does the rest: the new item's audio events
(`Audio::TriggerEvents`), reweighting the theme bank and director for the new
program (`set_program`), and re-applying the program's entrainment bed
(`SetEntrainment`).

**Known hole:** a weighted cycle of standard items that all have `play_time_seconds`
of 0 never satisfies the hold condition, so `advance()` loops forever, firing audio
events every pass. Only subroutine depth is bounded; there is no iteration cap.

### Summary

| Construct | Proto | Behaviour |
|---|---|---|
| Standard item | `PlaylistItem.Standard` | Play `program` for `play_time_seconds`, then branch. |
| Branch | `PlaylistItem.NextItem` | Weighted-random pick of the next item among enabled branches. |
| Conditional | `NextItem.condition_variable_*` | Branch enabled only when a session variable matches. |
| Subroutine | `PlaylistItem.Subroutine` | Push named items in order; pop back when each dead-ends. |
