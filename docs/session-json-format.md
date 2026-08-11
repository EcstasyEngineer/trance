# Session JSON format — normative spec (format_version 1)

**Status: normative.** This is the on-disk format for trance sessions and system config,
replacing the text-format protobuf `.session` / `.cfg` files.

**Scope of this wave:** the on-disk format changes; the **in-memory model remains
`trance_pb::Session` / `trance_pb::System`** (`src/common/trance.proto`). The JSON loader
populates the proto structs and runs the existing `validate_session()` /
`validate_system()` repair passes (`src/common/session.cpp:448,486`) unchanged. Replacing
trance_pb with native structs is a named future step — the **trance_pb retirement wave** —
which lands once the legacy auto-migration path (`session_legacy.cpp`) is the *only*
remaining proto consumer: i.e. when the validate/repair passes and every runtime reader
work on native structs, leaving the proto purely as the migration target the frozen
legacy schema translates into. Until then, every JSON field below
has an exact proto mapping, and the proto file stays the schema of record for in-memory
shapes.

Design lineage: verbatim-proto-names living-subset spine, with the hand-editing surface
(pattern files, hex colours, comment keys, `scan` themes) grafted on, and dead proto
structures (`SessionArchive`, deprecated fields, `OCULUS`) cut.

---

## 1. File layout and naming

```
<session-root>/                      # parent dir of the session file = media root (unchanged rule)
  my-session.session.json            # one Session; multiple sessions may share a root
  patterns/                          # convention dir for v3 pattern sources (any root-relative path works)
    breathe.pattern
    strobe-drop.pattern
  <theme dirs>/*.png|webm|gif|ttf    # media, referenced root-relative (unchanged)
  audio/*.ogg|wav|flac|aiff          # convention only

<exe dir>/
  system.json                        # replaces system.cfg; runtime-written, gitignored
```

| Thing | Name |
|---|---|
| Session file | `*.session.json` (double extension: JSON tooling + human-readable purpose) |
| Default session | `default.json` (`DEFAULT_SESSION_PATH`, `src/common/common.h`) — auto-created on a no-arg cold start, auto-migrated from a legacy `./default.session` when one is present (main.cpp) |
| System config | `system.json` next to the exe (`SYSTEM_CONFIG_PATH`, `common.h:7`) |
| Pattern source | `*.pattern`, plain UTF-8 v3 DSL text. Named by what it is, **not** by grammar revision — a future grammar bump must not force a mass rename. |
| Archive | `*.trance` — a zip of the session root with the session file stored as `session.json` at zip root. Written by `--export_archive`; see §9.1. |

There is **no** directory-per-session convention and **no** fixed `session.json` filename
outside archives. A session is a file; its parent directory is the media root
(`main.cpp:573`). This preserves the existing multiple-sessions-per-media-root layout.

**Path contract (zip-readiness):** every path in a session file — media, audio, pattern
files, `scan` targets — MUST be root-relative, forward-slash, with no `..` segments and no
absolute paths. The loader **errors** on violations; the saver normalizes backslashes to
forward slashes and refuses to write escaping paths. This makes the future archive
literally `zip -r` of the session root: zip member names ARE the reference strings.

## 2. Common encoding rules

These apply to both `session.json` and `system.json`.

1. **Keys are the proto field names, verbatim** (`program_map`, `next_item`,
   `condition_variable_name`, …). Same purpose = same name; no parallel JSON vocabulary.
   The only structural deviations from the proto are the five transforms listed in §7.
2. **Enums are lowercase strings** of the proto enum value name: visual types
   `accelerate | slow_flash | sub_text | flash_text | parallel | super_parallel |
   animation | super_fast`; audio event type
   `play | stop | fade` (the redundant `AUDIO_` prefix is dropped). Integers are not
   accepted. Note: the CLI `--visual` name table (`main.cpp:463-473`) calls PARALLEL
   `simple` — the JSON parser must NOT reuse that table; JSON says `parallel`.
3. **Colours are hex strings**, `"#RRGGBB"` (alpha = FF) or `"#RRGGBBAA"`. This is the
   only colour form — no float-object alternative. Mapping to `trance_pb::Colour` floats:
   component = byte / 255 on load, byte = round(float × 255) on save. All existing content
   was authored through `make_colour`'s /255 path (`session.cpp:14`), so quantization is
   lossless in practice.
4. **Comment keys:** any key beginning with `_` (`"_note"`, `"_todo"`) is ignored by the
   loader at every nesting level. This is the sanctioned comment mechanism.
5. **Strict unknown-key errors:** any non-`_` key not in this spec is a load **error**,
   with the offending JSON path in the message. This is the typo guard — without it a
   misspelled key is silently ignored and `validate_session()`'s leniency turns the
   mistake into "why is my program default pink".
6. **Absent key = proto default** (0 / 0.0 / false / "" / empty). The saver omits fields
   equal to the proto default. `validate_*` refills load-bearing defaults exactly as it
   does today — the JSON layer adds no repair logic.
7. **Version wrapper:** both files carry `"format"` (discriminator string) and
   `"format_version"` (integer, currently `1`). Loader errors on a greater version or a
   wrong discriminator. Additive new keys do NOT bump the version; only breaking shape
   changes do.

## 3. `session.json` schema

Top level (`trance_pb::Session`, trance.proto:275):

| Key | Type | Proto mapping |
|---|---|---|
| `format` | string, must be `"trance-session"` | — |
| `format_version` | int, must be `1` | — |
| `first_playlist_item` | string | `Session.first_playlist_item` |
| `playlist` | object: name → playlist item | `Session.playlist` (map) |
| `program_map` | object: name → program | `Session.program_map` |
| `theme_map` | object: name → theme | `Session.theme_map` |
| `theme_scan_root` | string **or** object, optional | no proto field — loader expansion, see below |
| `variable_map` | object: name → variable | `Session.variable_map` |

**`theme_scan_root`** makes the theme SET itself derived from a directory tree, rather
than a fixed manifest. Two forms:

```json
"theme_scan_root": "."                              // auto-rescan ON (the default)
"theme_scan_root": { "dir": ".", "auto_rescan": false }
```

With it set and `auto_rescan` on, every load re-derives which directories are themes: a
folder added since the last save **becomes a theme**, enabled at weight 1 in every program
(and only if that program has no row for the name already — a duplicate row would make the
theme's effective weight order-dependent). This is the theme-level half of the
inverted-persistence rule that `scan.exclude` (§3.3) provides at the file level — the
session records what to leave OUT, so content that appears on disk is IN by default.

Discovery **only ever adds**. A scan theme whose directory has gone missing is deliberately
left alone rather than removed: "the folder was deleted" and "the folder is not mounted
right now" are indistinguishable from here, and removing it would discard that theme's
weight, pin, `inherit` flag and exclusion list on the next save — unrecoverably, and
triggered by nothing more deliberate than launching on a laptop with a drive unplugged. The
cost is only untidiness: a genuinely deleted folder leaves a theme that expands to nothing
(so draws nothing) until it is removed in the F2 Themes section.

Without it, `scan` keeps each *existing* theme's contents current but a brand-new folder
stays invisible until someone regenerates the session by hand. That asymmetry is the bug
this key exists to remove: the cold-start scrape writes `"theme_scan_root": "."` so a
generated session is live from creation.

`auto_rescan: false` freezes the theme LIST while leaving each theme's own `scan` live —
new *files* still appear, new *folders* do not.

Only ADDs and provably-dead removals happen. Themes with explicit media lists and no
`scan` entry are never touched, and an existing theme keeps its weight, pin, `inherit` and
`exclude` settings across a rescan because all of those are keyed by theme name.

### 3.1 Playlist item (`trance_pb::PlaylistItem`)

Exactly one of `standard` / `subroutine` must be present (the proto `oneof contents`);
both present or both absent is a load **error** (key presence is looser than a proto
oneof, so the check is explicit).

| Key | Type | Proto mapping |
|---|---|---|
| `standard` | object | `PlaylistItem.standard` (oneof) |
| `standard.program` | string, program name | `Standard.program` |
| `standard.play_time_seconds` | uint | `Standard.play_time_seconds`; 0/absent = play until a branch fires |
| `subroutine` | array of playlist item names | `PlaylistItem.subroutine.playlist_item_name` — the single-field `Subroutine` message is **flattened** to a bare array |
| `next_item` | array of objects | `PlaylistItem.next_item` |
| `next_item[].playlist_item_name` | string | `NextItem.playlist_item_name` |
| `next_item[].random_weight` | uint | `NextItem.random_weight` |
| `next_item[].condition_variable_name` | string, optional (both condition keys or neither) | `NextItem.condition_variable_name` |
| `next_item[].condition_variable_value` | string | `NextItem.condition_variable_value` |
| `audio_event` | array of objects | `PlaylistItem.audio_event` |
| `audio_event[].type` | `"play"` \| `"stop"` \| `"fade"` | `AudioEvent.type` (`AUDIO_PLAY/STOP/FADE`; `NONE` has no JSON form) |
| `audio_event[].channel` | uint | `AudioEvent.channel` |
| `audio_event[].next_unused_channel` | bool (play only) | `AudioEvent.next_unused_channel` |
| `audio_event[].path` | string, root-relative | `AudioEvent.path` |
| `audio_event[].loop` | bool (play only) | `AudioEvent.loop` |
| `audio_event[].volume` | uint 0–100 (play: initial; fade: target) | `AudioEvent.volume` |
| `audio_event[].time_seconds` | uint (fade only) | `AudioEvent.time_seconds` |

The deprecated flat `PlaylistItem.program` (field 100) and `play_time_seconds` (101) have
**no JSON representation**. The converter migrates them (§7); the loader rejects them as
unknown keys at the item's top level... they are only legal inside `standard`.

### 3.2 Program (`trance_pb::Program`)

| Key | Type | Proto mapping |
|---|---|---|
| `enabled_theme` | array | `Program.enabled_theme` |
| `enabled_theme[].theme_name` | string | `EnabledTheme.theme_name` |
| `enabled_theme[].random_weight` | uint | `EnabledTheme.random_weight` |
| `enabled_theme[].pinned` | bool; at most one true per program (`validate_program` enforces) | `EnabledTheme.pinned` |
| `visual_type` | array | `Program.visual_type` |
| `visual_type[].type` | enum string, see §2.2 | `VisualTypeConfig.type` |
| `visual_type[].random_weight` | uint | `VisualTypeConfig.random_weight` |
| `visual_type[].pinned` | bool, omitted when false; at most one true across `visual_type` **and** `custom_visual_pattern` (`validate_program` enforces) | `VisualTypeConfig.pinned` |
| `custom_visual_pattern` | array | `Program.custom_visual_pattern` |
| `custom_visual_pattern[].name` | string, unique within the program (load error on duplicate) | `VisualPatternSource.name` — authoritative identity; NOT derived from the filename |
| `custom_visual_pattern[].file` | string, root-relative path to a `*.pattern` file | file **content** → `VisualPatternSource.source_text` (see §4) |
| `custom_visual_pattern[].random_weight` | uint | `VisualPatternSource.random_weight` |
| `custom_visual_pattern[].enabled` | bool | `VisualPatternSource.enabled` |
| `custom_visual_pattern[].pinned` | bool, omitted when false; shares the single-pin budget with `visual_type[].pinned`; cleared on a disabled pattern | `VisualPatternSource.pinned` |
| `global_fps` | uint, clamped 1–240 by validate | `Program.global_fps` |
| `zoom_intensity` | float, clamped 0–1 | `Program.zoom_intensity` |
| `spiral_colour_a` / `spiral_colour_b` | hex colour string | `Program.spiral_colour_a/b` |
| `reverse_spiral_direction` | bool | `Program.reverse_spiral_direction` |
| `main_text_colour` / `shadow_text_colour` | hex colour string | `Program.main_text_colour` / `shadow_text_colour` |
| `entrainment` | object; **absent key = no bed** | `Program.entrainment` |
| `entrainment.master_db` | float dB RMS; 0/absent = default −28 | `Entrainment.master_db` |
| `entrainment.layer` | array | `Entrainment.layer` |
| `entrainment.layer[].center_hz` | float Hz | `EntrainmentLayer.center_hz` |
| `entrainment.layer[].binaural_hz` | float Hz, 0 = mono carrier | `EntrainmentLayer.binaural_hz` |
| `entrainment.layer[].pulse_hz` | float Hz, 0 = continuous | `EntrainmentLayer.pulse_hz` |
| `entrainment.layer[].amplitude_db` | float dB, 0 = unity | `EntrainmentLayer.amplitude_db` |

The deprecated `Program.enabled_theme_name` (field 100) has no JSON representation.
There is **no inline pattern-source key** — `file` is the only form (§4).

**Weights are raw and unnormalized.** Every `random_weight` is a lottery ticket count,
not a percentage: a row's real share is `weight / total` over its pool. There are two
pools, and they are the two the F2 panel shows percentages against — themes
(`enabled_theme`) and visuals (`visual_type` **plus** enabled `custom_visual_pattern`
entries, which `Director::change_visual` draws from as a single combined lottery).
A pool that sums to **0** is the magic empty state: `validate_program` rewrites an
all-zero theme pool to every theme at weight 1, and an all-zero *unpinned* visual pool
to the default visual types.

**A theme's `random_weight` does two jobs.** Besides its share of the theme-rotation
lottery, it is also that theme's **tier weight** when its content is inherited by a
descendant (`scan.inherit`, §3.3). Both answer the same question — how much do I want to
see this — so they are deliberately one number rather than two.

Inside an inheriting theme, selection picks the **source tier first, by weight**, then an
image within that tier. It is *not* a flat draw over the merged pool. This is what makes
the weights mean what they appear to mean: a flat union samples by raw file count, so with
5 images in `a/b` (weight 4) and 5 in `a` (weight 1) a flat draw gives 50/50 and the
weights are discarded entirely — and a 10-image folder inheriting a 280-image parent shows
the parent ~97% of the time no matter how it is weighted. Directory size would silently
override intent. With tiered selection the same 4:1 gives ~80/20 (measured 81.4/18.6 over
2046 picks), independent of how many files each folder happens to hold.

Tiers come from the inheritance chain in pool order — tier 0 is the theme's own content,
then each ancestor — and weights are normalized over whichever tiers the chain actually
reaches. If `spam` (20) inherits `hypno` (4) but `hypno` does not inherit the root, `spam`
draws 83%/17% from the two tiers present, not 80/16/4.

Two behaviours worth stating because they are not derivable from the above:

- An ancestor whose theme carries **no rotation weight, or is switched off entirely, still
  contributes its tier at weight 1** rather than 0. Inheriting a folder is a separate,
  explicit choice from wanting that folder in rotation, so disabling it as a live theme
  must not silently empty it out of its descendants' pools.
- Tier weighting governs the mix **within one theme's pool**, not what is on screen. The
  engine keeps two themes live and a pinned theme holds only one of the two slots, so the
  visible mix is the tier distribution composed with the ordinary theme rotation.

Only `image_path` is tier-weighted. Animations, text and audio still draw flat from the
merged pool.

**`pinned` means different things per pool.** A pinned *theme* is always one of the two
live themes — presence, not rotation share — so it stays resident even at weight 0 and
the weights only choose the other slot. A pinned *visual* is force-this-one: selection
skips the weight lottery entirely and always returns it (the `--visual` / `--pattern`
CLI overrides still win over a session pin, and a pin whose visual can't be compiled
falls through to the normal lottery rather than freezing).

### 3.3 Theme (`trance_pb::Theme`)

| Key | Type | Proto mapping |
|---|---|---|
| `scan` | string **or** object, optional | no proto field — loader expansion, see below |
| `image_path` | array of root-relative paths | `Theme.image_path` |
| `animation_path` | array | `Theme.animation_path` |
| `font_path` | array | `Theme.font_path` |
| `text_line` | array of strings; embedded `\n` = pre-split lines, as today | `Theme.text_line` |
| `audio_path` | array of root-relative paths; precanned audio (mantras/cues) this theme owns — the grammar decides when/volume (issue #23) | `Theme.audio_path` |

**There is exactly ONE theme model: a directory plus a blacklist.** A theme is always the
files directly inside one directory, minus whatever it excludes. There is no second mode, no
frozen-list mode, and consequently no conversion between modes. A legacy session carrying
explicit `image_path` lists is migrated to this model automatically at load (see below).

`scan` has a shorthand and a long form. The **string** shorthand `"scan": "dir"` is just the
directory. The **object** form adds the optional parts:

```json
"scan": {
  "dir": "hypno/spam",              // required, root-relative directory
  "inherit": false,                 // optional, DEFAULT false -- see below
  "exclude": ["hypno/spam/x.jpg"]   // optional, root-relative paths held out
}
```

A scan is never recursive, in either form: one directory is one theme, so recursing would
make a parent swallow its children's content when those children are themes in their own
right, and put every nested file in two pools at once.

Sessions written before the folder hierarchy landed used the string form for a whole-subtree
walk. Their nested content is not lost, it is **redistributed**: the scan root's discovery
pass turns each subdirectory into a theme of its own at weight 1, and `inherit` is how you
fold it back together where the old shape was actually wanted. A directory that holds
nothing but subdirectories consequently expands to an empty theme; the runtime keeps an
empty theme out of the rotation rather than giving it frames it can only draw black.

**`inherit`** folds the theme named by this theme's parent DIRECTORY into its pool, and it
**composes transitively**: `hypno/spam` reaches the root's loose files only if `hypno`
inherits too. Turning it off everywhere makes each theme exactly its own folder; turning it
on everywhere makes each theme its own folder plus its whole ANCESTOR chain; anything
between is reachable one flag at a time.

Note this is *not* the same as the pre-hierarchy behaviour, and cannot be: the old scrape
gave `hypno` its entire SUBTREE (everything under `hypno/**`) plus the root's loose files,
whereas inheritance only ever flows DOWNWARD from ancestors. A theme never picks up its
descendants or its siblings — a descendant is its own theme with its own weight.

Directories with no theme of their own (pure containers) are
skipped, so an intermediate container never breaks a chain. The set of themes is identical
either way — only pool composition changes — so toggling `inherit` can never strand a
saved per-theme weight. Resolution happens at load, after every theme exists, so the
runtime never sees the distinction.

**`exclude`** inverts persistence: a scan theme records what to leave OUT, so a file added
to the folder later is picked up with no edit. This is the point of the object form — a
frozen `image_path` list silently ignores everything added after it was written. Paths are
compared post-rebase (root-relative, the same form every other reference uses).

An exclusion applies to the theme's **own** expansion only. An inherited image has to be
excluded on the theme that owns it — that is where the scan producing it runs — and the F2
Themes section reflects that: own images get a checkbox, inherited ones are listed with the
theme to go and exclude them on.

Exclusions are **never dropped automatically**, on load or on save. A path the scan did not
produce is equally "deleted" and "not synced yet", and pruning against a half-materialized
cloud folder would take the whole list with it. They accumulate at one short string per file
ever excluded; unchecking is undone by re-checking the box in F2. (Same call as the
never-remove-a-missing-theme rule below: untidy beats unrecoverable.)

Inherited content is **tier-weighted, not merged flat**: selection picks the source theme
by its `random_weight` first and an image within it second, so a small folder inheriting a
large one is not swamped by raw file count. See "Weights are raw and unnormalized" in §3.2
for the full rule, including what happens when an ancestor is switched off.

**Legacy migration.** A theme carrying explicit `image_path` lists and no `scan` is
converted to a folder theme at load, whenever a directory of that theme's name exists. The
frozen list is **discarded, not preserved as exclusions**: a file on disk that the list does
not name is far more often one added since the session was written than one deliberately
omitted, and turning it into a permanent exclusion would reproduce the exact bug this model
exists to remove. Deliberate omission has a real representation now (`exclude`); a legacy
frozen list has none, so there is nothing to carry across. A theme with no directory of its
name is left alone — it is a hand-written list spanning unrelated folders, which no single
directory can reproduce, and it is the one shape this model does not cover.

The directory has to actually *produce* something for the conversion to happen. A folder
that exists but expands to nothing — a pure container, a folder of nothing but denylisted
junk, a drive that is mounted but not yet synced — leaves the theme exactly as it was.
Adopting it would clear the curated list, write `{"scan": ...}` on the next save, and delete
the only copy of that list from disk.

**`/root/`** is a reserved theme name for loose files sitting directly at the scan root. A
path component can never contain a slash, so it cannot collide with a real directory's
theme name. It is a first-class theme with its own rotation weight — it replaces the
retired `/wildcards/` pseudo-theme, which was merged into every other theme and then
erased, diluting every theme with the same content and (because no single directory then
reproduced a theme) disabling `scan` persistence for the whole folder.

At load, the loader walks the directory with
`search_resources(trance_pb::Theme&, root)` (`session.cpp`) and **appends** what it finds
to the explicit lists above (explicit entries first, scan results after, scan order =
directory-walk order). Path results are rebased onto the scan directory before they land
in trance_pb, so they are root-relative like every other reference (§1) — a `scan` of
`media` holding `a.png` yields `media/a.png`, not `a.png`. The `scan` key itself never
reaches trance_pb; the loader records it in the save sidecar (§5) so the editor re-emits
`scan` rather than freezing the expansion into explicit lists. This is the drop-a-folder
modding workflow: add `themes/ocean/` full of media, write a 1-line theme entry. Scanned
themes are resolved per load — the manifest does not pin the media list; inside a
`.trance` archive the zip contents define the scan result.

**A scanned folder is COMPLETE content (#36).** The walk fills *every* theme list, not
just the path ones: `.txt` files become `text_line` entries (one per non-blank line,
uppercased and split at the space nearest the middle — the same treatment the cold-start
directory scan has always applied), and audio files become `audio_path`. A theme whose
content is entirely scan-derived therefore round-trips as just its `scan` entry — the
saver writes no `image_path`/`animation_path`/`font_path`/`text_line`/`audio_path` for it,
because reloading re-derives all five and writing them would double every entry.

**No filename filtering (#36).** "If it's in the folder, that's the content." Extensions
are a *dispatch*, not an allowlist:

| Extension | Goes to |
|---|---|
| `webm`, `gif` | `animation_path` |
| `ttf` | `font_path` |
| `txt` | `text_line` |
| `wav`, `ogg`, `flac`, `aiff` | `audio_path` |
| **everything else** | `image_path` |

An unrecognized file with a plausible extension is scanned as an image on purpose: the
decode layer already tolerates junk — a file that won't decode is marked `failed` once,
dropped from the draw pool, and never retried — so an allowlist buys nothing but
silently-missing media. A `.xyz` stays content.

What a scan skips is a **junk denylist**, not a media allowlist. It covers only things
that recur in real media folders and are never content:

| Skipped | Why |
|---|---|
| `.json`, `.session`, `.pattern`, `.cfg`, `.trance` | Session machinery — a session file living next to its media must never become a phantom image. |
| Dotfiles, and anything under a dotted directory (`.git`, `.DS_Store`) | Never content. |
| `thumbs.db`, `*.ini`, `*.log` | OS/shell droppings and config. `.ini` matters specifically because **trance itself** writes `imgui.ini` (the F2 panel's window state) into the working directory, so any folder you have ever played would otherwise scan a phantom of your own making. |
| `*.bak`, `*.tmp`, `*.swp`, `*.part`, `*.crdownload`, and any name ending in `~` | Editor backups and partial downloads. These usually *shadow* a real media file (`foo.png.bak`), so admitting them double-counts the content. |
| **Extensionless files** (`README`, `LICENSE`, `Makefile`, lock files) | The classifier's fall-through would call every one of them an image. Nothing trance can play is extensionless, so this costs no real media. **Accepted tradeoff:** SFML could in principle decode an extensionless file; extensionless files recur as junk far more often than as intentional media. Rename the file if it is real content. |

The implementation is `is_scan_ignored` (`src/common/session.cpp`).

**Cold start.** The no-arg bootstrap (no `./default.json`, no `./default.session`) scans the
working directory and builds **one theme per directory that directly contains at least one
file**, at any depth, named by its root-relative path (`hypno`, `hypno/spam`). A directory
holding only other directories is a pure container and produces no theme. Loose files at the
root become the reserved `/root/` theme.

Every theme is written as a `scan` theme, and the session also gets
`"theme_scan_root": "."` (§3) — so `default.json` stays a folder reference in both senses:
each theme tracks its own directory's files, and the theme SET itself is re-derived on every
load, so a directory added later becomes a theme without touching the file.

There is no fallback to explicit lists. The retired `/wildcards/` pseudo-theme used to merge
loose root files into every theme, which meant no single directory reproduced a theme's
content and the bootstrap had to freeze explicit lists — the exact behaviour that made media
added later invisible. With no cross-theme merge, every theme is exactly one directory's own
files and is always reproducible from it.

### 3.4 Variable (`trance_pb::Variable`)

| Key | Type | Proto mapping |
|---|---|---|
| `description` | string | `Variable.description` |
| `value` | array of strings | `Variable.value` |
| `default_value` | string, must be in `value` (validate repairs) | `Variable.default_value` |

`description` and `default_value` are kept even though the 2017 player never read them:
the launch-time variable picker moves into trance.exe with the ImGui editor. Additionally,
at session start any variable not supplied on the command line or in the launch UI is
seeded with its `default_value` — closing the existing gap where an unset variable
compared as `""` in `is_enabled` (`session.cpp:286-298`).

### 3.5 Minimal example

```json
{
  "format": "trance-session",
  "format_version": 1,
  "_note": "keys starting with _ are comments",
  "first_playlist_item": "main",
  "playlist": { "main": { "standard": { "program": "default" } } },
  "program_map": {
    "default": {
      "enabled_theme": [ { "theme_name": "all", "random_weight": 1 } ],
      "visual_type": [ { "type": "slow_flash", "random_weight": 1 },
                       { "type": "animation", "random_weight": 1 } ],
      "custom_visual_pattern": [
        { "name": "breathe", "file": "patterns/breathe.pattern",
          "random_weight": 3, "enabled": true } ],
      "global_fps": 120,
      "zoom_intensity": 0.5,
      "spiral_colour_a": "#FF96C832",
      "spiral_colour_b": "#00000032",
      "main_text_colour": "#FF96C8E0",
      "shadow_text_colour": "#000000C0",
      "entrainment": {
        "master_db": -28.0,
        "layer": [
          { "center_hz": 312.0, "binaural_hz": 3.0, "pulse_hz": 5.0 },
          { "center_hz": 60.0, "binaural_hz": 3.0, "pulse_hz": 3.25, "amplitude_db": -6.0 } ]
      }
    }
  },
  "theme_map": { "all": { "scan": "media" } }
}
```

## 4. Pattern placement — files only

v3 pattern sources live in standalone `*.pattern` files (plain text, `#` line comments
already supported by the tokenizer). The JSON holds only the per-program selection knobs
(`name`, `file`, `random_weight`, `enabled`) — per-program because two programs can share
one pattern file with different weights.

There is deliberately **no inline `source_text` key**. One representation, no fallback
paths. Patterns are the modding surface; a text file beats a JSON-escaped string, and
"paste from docs into a file" is the intended flow.

Rules:
- `name` is the pattern's identity (F1 overlay label, uniqueness within the program).
  Renaming the file does not change the identity; only the `file` reference updates.
- Loader reads the file at session load and puts its content into
  `VisualPatternSource.source_text` — the director then consumes it unchanged
  (`director.cpp:136-141`: disabled patterns skipped, parse failures skipped with a
  surfaced warning, never a crash).
- A **missing or unreadable** pattern file is a load error (broken manifest), distinct
  from a parse failure (skip + warn at compile time).
- Multiple `custom_visual_pattern` entries may reference the same `file`.

## 5. Save behavior and the pattern/scan sidecar

`trance_pb` has no `source_file` field and no `scan` field (the in-memory model is frozen
this wave), so the loader keeps a per-session **sidecar** alongside the loaded proto:

- `{program name, pattern name} → pattern file path`
- `theme name → scan directory` (when present)

The saver consults the sidecar: pattern text is written back to its recorded file (not
inlined), and `scan` keys are re-emitted — a scanned theme's expansion is NOT written into
the explicit lists at all (§3.3: the walk fills all five lists, so a pure scan theme saves
as `{"scan": "<dir>"}` alone). *Known limitation:* the saver cannot tell a scanned theme's
explicit entries apart from its expansion, so hand-written `image_path`/`text_line`/… next
to a `scan` do not survive an editor save. Put the extra media in the folder, or drop the
`scan`. This dissolves when `scan` becomes a real proto field (#36). A pattern created in
the editor with no file yet gets
`patterns/<slug>.pattern`, slug = lowercase name mapped to `[a-z0-9_-]` (other chars →
`_`), collisions suffixed `-2`, `-3`, …

The sidecar lives in the load/save layer (`src/common/session.{h,cpp}`), not in trance_pb.
At the trance_pb retirement wave it dissolves into real fields on the native structs.

Writer output: 2-space indent, keys in schema order, default-valued fields omitted (§2.6).

Saves are atomic: the writer fills a `<path>.tmp` sibling and renames it over the target
(`write_file_atomically`, session_json.cpp), so an interrupted write leaves the previous
file intact rather than a truncated one. `.tmp` is on the scan-ignore list (§3.3), so a
temp file left behind by a killed process is never mistaken for content.

**Known limitation — comments do not survive a save, and the F2 panel now saves by
itself.** The save path serializes from the proto + sidecar, so `_`-prefixed comment keys
and hand-chosen key ordering are dropped. This used to cost a deliberate Save click; as of
the autosave model (see [controls.md](controls.md)) *any* edit made in the F2 panel
rewrites the loaded file, so a session you want to keep commented should be edited in a
text editor and not opened for tweaking in F2 — or kept as a copy that the app does not
load. Comment preservation is deferred to the trance_pb retirement wave (the native model
can carry them); do not build a JSON-DOM patching layer this wave.

## 6. `system.json` schema

`trance_pb::System` (trance.proto:31), with the presence-wrapper messages flattened and
the `LastSession` wrapper flattened. `system.json` is the **single** runtime-written
config — there is no separate editor-state file; the fields the old wx creator used move
here with the ImGui editor (which lives inside trance.exe).

```json
{
  "format": "trance-system",
  "format_version": 1,
  "enable_vsync": true,
  "windowed": false,
  "draw_depth": 0.5,
  "eye_spacing": 0.0625,
  "image_cache_size": 64,
  "animation_buffer_size": 32,
  "font_cache_size": 8,
  "last_root_directory": "D:/sessions",
  "last_export_settings": {
    "path": "out.webm", "export_3d": false,
    "width": 1280, "height": 720, "fps": 30,
    "length": 60, "quality": 2, "threads": 4
  },
  "last_session_map": {
    "D:/sessions/deep.session.json": { "Mode": "Deep" }
  }
}
```

| Key | Type | Proto mapping |
|---|---|---|
| `enable_vsync` | bool | `System.enable_vsync` |
| `windowed` | bool | `System.windowed` |
| `draw_depth` | float 0–1; absent = default 0.5 | `System.draw_depth.draw_depth` — wrapper **flattened**; key presence carries the `has_draw_depth()` distinction (`validate_system`, session.cpp:450), so `0.0` present and key absent behave differently, as today |
| `eye_spacing` | float −1–1; absent = default 0.0625 | `System.eye_spacing.eye_spacing` — flattened, same presence rule |
| `image_cache_size` | uint, min 16 | `System.image_cache_size` |
| `animation_buffer_size` | uint, min 8 | `System.animation_buffer_size` |
| `font_cache_size` | uint, min 2 | `System.font_cache_size` |
| `last_root_directory` | string, absolute OK (machine-local file) | `System.last_root_directory` |
| `last_export_settings` | object, all `ExportSettings` fields verbatim (`path`, `export_3d`, `width`, `height`, `fps`, `length`, `quality` 0–4, `threads`). **Deliberately-dead schema:** the video-export path it configured has been deleted and nothing reads these values. The key stays specified because `get_default_system()` writes it into every `system.json` and §2.5's strict loader throws on an unknown key — so removing it from the spec would make every existing config unloadable | `System.last_export_settings` |
| `last_session_map` | object: session path → object: variable → value | `System.last_session_map`; the single-field `LastSession{variable_map}` wrapper is **flattened** to a direct string→string object |

**Removed key: `renderer`.** It mapped to `System.renderer` (proto field 2, now
`reserved 2;`). VR output is automatic and unconfigurable — there is one renderer and the
headset is an output of it (`docs/spec-xr-unified.md` D2). The key got **no** back-compat
allow-list entry, so a `system.json` still carrying it fails §2.5's strict key check and
is regenerated with defaults, losing its other settings. That is the accepted, spec'd cost
of the greenfield config rule, not an oversight.

`last_root_directory` and `last_session_map` are unread by the 2017 player but are the
ImGui editor's file-dialog memory and per-session variable memory (a behaviour inherited
from the deleted wx creator). They stay in system.json — do not invent a second state file
for them. `last_export_settings` is now read by nothing at all; see its table row.

## 7. Legacy migration

There is no converter binary: **loading is converting.** `trance.exe` given a legacy
`.session` path (or finding a sibling `./default.session`) migrates it to a sibling
`<name>.session.json` (plus externalized `patterns/*.pattern`) transparently on load
(`convert_legacy_session`, session.cpp), and startup migrates a sibling legacy
`system.cfg` into `system.json` when no loadable `system.json` exists (main.cpp). For a
headless one-shot conversion — no window, no GL — run `trance --lint old.session`: lint
loads the session through the same path, which writes the JSON, and validates its
patterns for free. The original file is never overwritten.

**One importer, frozen schema.** Input is parsed as text-format protobuf against the
**frozen fork-point descriptor** (`src/common/legacy.proto`, package `trance_legacy_pb` —
a verbatim copy of `trance.proto` at 0e97381, the last upstream commit), then translated
field-by-field into the live `trance_pb` model in one place, `session_legacy.cpp`.
Upstream-era files import unchanged; a file carrying a field this fork added
(`entrainment`, `custom_visual_pattern`, theme `audio_path`, `renderer: OPENXR`) is
**rejected with a diagnostic naming the field**, not half-read — fork-era files are
already JSON and are not legacy input. Pinning the schema keeps the import contract
permanent: changes to the live `trance.proto` can never silently widen what the importer
accepts (issue #47).

Pipeline:
1. `TextFormat::ParseFromString` against the frozen `trance_legacy_pb` descriptor;
   explicit translation into `trance_pb` (`session_legacy.cpp`).
2. `validate_session()` / `validate_system()` — this is where legacy/dead proto fields are
   handled; the JSON emitter never sees them:
   - `Program.enabled_theme_name` (100) → `enabled_theme` entries, weight 1
     (session.cpp:110-115), then cleared.
   - flat `PlaylistItem.program` (100) / `play_time_seconds` (101) → `standard{}`
     (session.cpp:164-169), then cleared.
   - dangling theme/program/playlist/variable refs pruned or defaulted; clamps applied.
3. Emit JSON per this spec: maps → objects, repeated → arrays, enums → lowercase strings,
   colours → hex (round(f×255)), wrappers flattened (`draw_depth`, `eye_spacing`,
   `subroutine`, `LastSession`), oneof → key presence, default-valued fields omitted.
4. Externalize each `custom_visual_pattern[].source_text` to
   `patterns/<slug(name)>.pattern` (slug rule in §5) and emit `file` references.
5. Drops (no JSON form, by design): `AudioEvent.Type.NONE` events (no-ops in audio.cpp),
   zero-weight entries validate already prunes, the whole legacy `System.renderer` field
   (still parsed by the frozen schema, translated into nothing — there is no renderer
   setting left), the entire `SessionArchive` message (dead schema — the shipped bundle
   format is the zip in §9.1, whose central directory subsumes the offset/length table),
   deprecated fields 100/101 (migrated above).
6. `system.cfg` → `system.json`: mechanical; `last_session_map` keys (old `.session`
   paths) are copied as-is — stale keys are a harmless convenience-cache miss.

**Acceptance test:** the migration contract is pinned by `session_json_test`'s legacy
suite (upstream-era files import field-for-field; fork-added fields reject; message
presence survives).

**Lossy by design:** `#` comments in hand-edited textproto files are not carried over
(TextFormat discards them at parse). Re-add them as `_note` keys after converting.

## 8. Loader dispatch and the ImGui editor

**Playback and the editor operate on `*.session.json` / `system.json`.** The legacy
textproto parser survives solely as the §7 one-shot migration; nothing downstream of
`load_session` ever sees a proto file. Constants change: `DEFAULT_SESSION_PATH =
"default.json"`, `SYSTEM_CONFIG_PATH = "system.json"` (`common.h`). A missing
`system.json` is regenerated from `get_default_system()` and saved, exactly as today
(`main.cpp:563-570`).

Load pipeline: parse JSON (strict per §2) → populate `trance_pb` → resolve pattern
`file`s into `source_text` + build sidecar → expand theme `scan` dirs → run
`validate_session()` / `validate_system()` unchanged. Everything downstream — playlist VM
in main.cpp, director, theme_bank, audio — consumes `trance_pb` objects and is untouched.

**What the ImGui editor reads/writes:**
- Reads/writes `*.session.json` through the same load/save layer (proto + sidecar). It is
  the only component that writes sessions; playback never does.
- **Autosaves.** The loaded file is live state: every committed edit (slider release,
  button, defocused text field) is written straight back to the path the session was
  loaded from. There is no Save button. `Export` writes a copy to another path and does
  not change which file is live. The command channel (`--command_port`) is the exception
  and edits memory only, by design — a remote control surface must not rewrite the
  user's session.
- Edits pattern text in a multiline widget; the save writes the text to the pattern's
  `file` (§5), not into the JSON.
- Reads/writes `system.json` for its own state: `last_root_directory` (file dialogs) and
  `last_session_map` (pre-filling the launch variable picker, keyed by session path).
- Surfaces loader strict-mode errors (unknown key, bad path, missing pattern file,
  duplicate pattern name, both-oneof) with their JSON paths, and director parse warnings,
  in the UI rather than the console.

## 9. Named steps beyond the JSON wave

### 9.1 Archive — SHIPPED (export only)

`*.trance` is a zip of the session root: the session file stored as `session.json` at the
zip root, plus every asset the session references (theme media, `custom_visual_pattern`
files resolved through the §5 sidecar, playlist `audio_event` paths) under its existing
root-relative path. The §1 path contract is what makes this work — zip member names ARE
the reference strings, so packing is conceptually a plain `zip -r`. Written by
`export_session_archive` (`src/common/session_archive.{h,cpp}`, miniz), invoked as
`trance.exe --export_archive out.trance my.session.json`. Members are **stored, not
deflated**: session media is already compressed, so deflating it again burns CPU on
multi-GB roots for almost no size win.

**There is deliberately no importer.** A `.trance` is a standard zip and member names are
exactly the root-relative paths `load_session_json()` already expects, so any zip tool
extracts it back into a valid session root in one step. That IS the import story — no
bespoke format the player must also read, and the bundle stays inspectable and moddable
by hand after extraction.

This replaces the `SessionArchive` proto message, which remains in `trance.proto` as dead
schema.

### 9.2 Theme audio (#23) — SHIPPED

An additive `audio_path` array on themes (§3.3), with the grammar-first `audio` effect
owning when and at what volume (`docs/authoring-v3-patterns.md`,
`docs/spec-grammar-v3.md` §4.14). Additive key — no `format_version` bump.

### 9.3 trance_pb retirement wave — not started

Native in-memory structs replace `trance_pb`; the §5 sidecar dissolves into real fields;
comment preservation across editor saves becomes possible. The spec in this file does not
change shape at that point — that is the test of the retirement wave.

## 10. Migration notes for existing sessions

1. Open `my.session` in the player once (or run `trance --lint my.session` for a
   headless one-shot); a `my.session.json` and a `patterns/` dir appear beside it. Media
   files are not moved — relative paths were already root-relative and copy through
   verbatim. Keep or delete the old `.session` afterward; playback reads only the JSON.
2. `system.cfg`: leave it next to the binary and the first launch migrates it into
   `system.json` (preserves cache sizes and variable memory), or just delete it — a
   fresh `system.json` regenerates with defaults.
3. Custom patterns previously embedded in the `.session` become `patterns/*.pattern`
   files named by pattern-name slug; edit those files directly from now on.
4. Sessions whose colours were hand-set to non-1/255-grid floats quantize to 8-bit on
   convert (visually identical; noted for round-trip exactness only).
5. Any `renderer:` line in an old `system.cfg` is read and discarded — the setting no
   longer exists, and the rest of the file still imports.
6. Any `#` comments in hand-edited textproto files are lost by the converter — re-add as
   `"_note"` keys.
7. Old launcher `.bat`/shortcut lines pointing at `.session` paths need the new
   `.session.json` path; `last_session_map` entries keyed by old paths simply stop
   matching (cosmetic — re-picking variables once repopulates them).
