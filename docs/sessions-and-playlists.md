# Sessions and playlists

A `.session` is a single serialized `trance_pb::Session` protobuf
(`src/common/trance.proto`). It holds everything a session needs except the media
files themselves (which it references by relative path). This document tours the
schema, the load path, theme shuffling, and — in detail — the playlist "VM" that
the README mentions but doesn't explain.

## Proto schema tour

The schema lives in `src/common/trance.proto`. The four headline concepts:

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

A `SessionArchive` (`:290`) is a session plus an embedded file archive, for
shipping a self-contained bundle. (Archive export is currently a stub —
`export_archive` in `main.cpp` returns 0.)

## Load path

`main()` (`src/trance/main.cpp:442`) loads the session:

1. `load_session(path)` (`src/common/session.cpp:465`) parses the proto, then runs
   `validate_session()`.
2. `validate_session()` (`session.cpp:486`) is the repair pass: it fills empty
   maps with defaults, migrates deprecated fields (`enabled_theme_name` →
   `enabled_theme`, the old flat `program`/`play_time_seconds` →
   `Standard`), clamps numeric ranges, prunes dangling subroutine references and
   conditional variables, and repoints `first_playlist_item` if it names a missing
   item. A program with no usable themes or no visual weights is given defaults
   (`validate_program`, `:108`).
3. On a load failure, `main()` falls back to `get_default_session()` plus
   `search_resources()`, which walks the directory next to the binary and builds a
   theme per subfolder (a `wildcards/` folder is merged into all themes). That is
   how `trance.exe` runs against a bare folder of images with no `.session`.

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
image picking, animations, and text lines; `ThemeInfo`, `theme_bank.h:92`). The
program's `enabled_theme` weights pick which themes become active; a `pinned`
theme is always one of the two active, with the weights choosing only the other.
The frame loop calls `change_themes()` when `swaps_to_match_theme()` reports a
pending swap (`main.cpp:276`); `set_program` reweights the shuffler when the
playlist switches programs.

## The playlist VM

The playlist is a directed graph of `PlaylistItem`s executed by a small stack
machine in `play_session()` (`src/trance/main.cpp`). The state is a
`std::vector<PlayStackEntry>` where each entry is `{item, subroutine_step}`
(`main.cpp:107`); it starts with the `first_playlist_item` pushed (`:111`). The
driver is the `while (true)` loop at `main.cpp:223`.

### Standard items and timing

A `Standard` item plays its `program` for `play_time_seconds`. The loop checks
elapsed wall-clock since the last switch (`main.cpp:227`); while a standard item's
time hasn't elapsed it keeps playing. When the time is up it follows a branch.

### Branching (`next_item`)

Each item has a list of `NextItem` branches (`trance.proto:238`). The next item is
chosen by **weighted random** over the *enabled* branches —
`next_playlist_item()` (`main.cpp:27`) sums `random_weight` across branches that
pass their condition, rolls `random(total)`, and returns the matching branch's
`playlist_item_name`. A branch with total weight 0 (or no branches) ends the
current item.

### Conditionals (`is_enabled`)

A branch can gate on a session variable. `NextItem.condition_variable_name` /
`condition_variable_value` reference an entry in `variable_map`;
`is_enabled(next, variables)` (`src/common/session.cpp:286`) returns true when the
variable has no condition, or when the live variable's value equals the required
value. Branches that fail the condition are dropped from the weighted pick — so
variables steer which paths through the playlist are reachable.

Variable values come from the `--variables` command-line flag, a
semicolon-separated `key=value` list parsed by `parse_variables`
(`main.cpp:309`, with `\` escaping for `;`, `=`, `\`). `creator` persists the last
values per session in `System.last_session_map` (`trance.proto:76`).

### Subroutines

A `Subroutine` item is a list of other item names (`trance.proto:225`). When the
loop reaches a subroutine entry it **pushes** the named child onto the stack and
advances the parent's `subroutine_step` (`main.cpp:232`). Each pushed child runs
as its own item (standard or another subroutine — they nest). When a child has no
enabled `next_item` and the stack is deeper than one, the loop **pops** back to
the parent and continues to the parent's next step (`main.cpp:256`). The stack is
bounded by `MAXIMUM_STACK` (256, `common/common.h`); overflow is reported and the
subroutine is abandoned rather than recursing forever (`main.cpp:234`).

Each switch (standard branch, subroutine push) also: fires the new item's audio
events (`Audio::TriggerEvents`), reweights the theme bank and director for the new
program (`set_program`), and re-applies the program's entrainment bed
(`SetEntrainment`).

### Summary

| Construct | Proto | Behaviour |
|---|---|---|
| Standard item | `PlaylistItem.Standard` | Play `program` for `play_time_seconds`, then branch. |
| Branch | `PlaylistItem.NextItem` | Weighted-random pick of the next item among enabled branches. |
| Conditional | `NextItem.condition_variable_*` | Branch enabled only when a session variable matches. |
| Subroutine | `PlaylistItem.Subroutine` | Push named items in order; pop back when each dead-ends. |
