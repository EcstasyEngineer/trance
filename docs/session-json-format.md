# Session JSON format — normative spec (format_version 1)

**Status: normative.** This is the on-disk format for trance sessions and system config,
replacing the text-format protobuf `.session` / `.cfg` files.

**Scope of this wave:** the on-disk format changes; the **in-memory model remains
`trance_pb::Session` / `trance_pb::System`** (`src/common/trance.proto`). The JSON loader
populates the proto structs and runs the existing `validate_session()` /
`validate_system()` repair passes (`src/common/session.cpp:448,486`) unchanged. Replacing
trance_pb with native structs is a named future step — the **trance_pb retirement wave** —
which lands after the ImGui editor reaches creator parity. Until then, every JSON field
below has an exact proto mapping, and the proto file stays the schema of record for
in-memory shapes.

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
| Default session | `default.session.json` (`DEFAULT_SESSION_PATH`, `src/common/common.h:6`) |
| System config | `system.json` next to the exe (`SYSTEM_CONFIG_PATH`, `common.h:7`) |
| Pattern source | `*.pattern`, plain UTF-8 v3 DSL text. Named by what it is, **not** by grammar revision — a future grammar bump must not force a mass rename. |
| Archive (future wave) | `*.trance` — a zip of the session root with the session file stored as `session.json` at zip root. See §9. |

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
   animation | super_fast`; renderer `monitor | openvr`; audio event type
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
| `variable_map` | object: name → variable | `Session.variable_map` |

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
| `custom_visual_pattern` | array | `Program.custom_visual_pattern` |
| `custom_visual_pattern[].name` | string, unique within the program (load error on duplicate) | `VisualPatternSource.name` — authoritative identity; NOT derived from the filename |
| `custom_visual_pattern[].file` | string, root-relative path to a `*.pattern` file | file **content** → `VisualPatternSource.source_text` (see §4) |
| `custom_visual_pattern[].random_weight` | uint | `VisualPatternSource.random_weight` |
| `custom_visual_pattern[].enabled` | bool | `VisualPatternSource.enabled` |
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

### 3.3 Theme (`trance_pb::Theme`)

| Key | Type | Proto mapping |
|---|---|---|
| `scan` | string, root-relative directory, optional | no proto field — loader expansion, see below |
| `image_path` | array of root-relative paths | `Theme.image_path` |
| `animation_path` | array | `Theme.animation_path` |
| `font_path` | array | `Theme.font_path` |
| `text_line` | array of strings; embedded `\n` = pre-split lines, as today | `Theme.text_line` |

**`scan` semantics:** at load, the loader walks the directory with the existing
`search_resources(trance_pb::Theme&, root)` (`session.cpp:372`) and **appends** the found
images/animations/fonts to the explicit lists above (explicit entries first, scan results
after, scan order = directory-walk order). The `scan` key itself never reaches trance_pb;
the loader records it in the save sidecar (§5) so the editor re-emits `scan` rather than
freezing the expansion into explicit lists. This is the drop-a-folder modding workflow:
add `themes/ocean/` full of images, write a 1-line theme entry. Scanned themes are
resolved per load — the manifest does not pin the media list; inside a `.trance` archive
the zip contents define the scan result.

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
inlined), and `scan` keys are re-emitted (scan-derived media entries are NOT written into
the explicit lists). A pattern created in the editor with no file yet gets
`patterns/<slug>.pattern`, slug = lowercase name mapped to `[a-z0-9_-]` (other chars →
`_`), collisions suffixed `-2`, `-3`, …

The sidecar lives in the load/save layer (`src/common/session.{h,cpp}`), not in trance_pb.
At the trance_pb retirement wave it dissolves into real fields on the native structs.

Writer output: 2-space indent, keys in schema order, default-valued fields omitted (§2.6).

**Known limitation:** the editor's save path serializes from the proto + sidecar, so
`_`-prefixed comment keys and hand-chosen key ordering do not survive an editor save.
Hand edits are safe as long as playback never writes the session — which holds today
(only the editor writes). Comment preservation is explicitly deferred to the trance_pb
retirement wave (native model can carry them); do not build a JSON-DOM patching layer
this wave.

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
  "renderer": "monitor",
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
| `renderer` | `"monitor"` \| `"openvr"` | `System.renderer`; `OCULUS` has no JSON form (dead — LibOVR removed; `main.cpp:138` already treats it as monitor) |
| `windowed` | bool | `System.windowed` |
| `draw_depth` | float 0–1; absent = default 0.5 | `System.draw_depth.draw_depth` — wrapper **flattened**; key presence carries the `has_draw_depth()` distinction (`validate_system`, session.cpp:450), so `0.0` present and key absent behave differently, as today |
| `eye_spacing` | float −1–1; absent = default 0.0625 | `System.eye_spacing.eye_spacing` — flattened, same presence rule |
| `image_cache_size` | uint, min 16 | `System.image_cache_size` |
| `animation_buffer_size` | uint, min 8 | `System.animation_buffer_size` |
| `font_cache_size` | uint, min 2 | `System.font_cache_size` |
| `last_root_directory` | string, absolute OK (machine-local file) | `System.last_root_directory` |
| `last_export_settings` | object, all `ExportSettings` fields verbatim (`path`, `export_3d`, `width`, `height`, `fps`, `length`, `quality` 0–4, `threads`) | `System.last_export_settings` |
| `last_session_map` | object: session path → object: variable → value | `System.last_session_map`; the single-field `LastSession{variable_map}` wrapper is **flattened** to a direct string→string object |

`last_root_directory`, `last_export_settings`, and `last_session_map` are unread by the
2017 player but are the ImGui editor's file-dialog memory, export-dialog defaults, and
per-session variable memory (replacing `creator/launch.cpp` behavior). They stay in
system.json — do not invent a second state file for them.

## 7. Converter

`trance_convert <path>` (a standalone binary — no window/GL dependencies, safe on headless
boxes) where path is a legacy `.session` or `.cfg`. Emits a sibling
`<name>.session.json` (plus externalized `patterns/*.pattern`) or `system.json`. Never
overwrites the input; errors rather than clobbering an existing output file.

**One converter, one schema.** Input is parsed as text-format protobuf with the **fork's
`trance.proto` descriptor** (existing files are textproto, not binary — `load_proto`,
session.cpp:231-243). Because the fork proto is a strict superset of the 2017 upstream
proto, original ohgodwhydidido-era files and fork files both parse with this one
descriptor; upstream files simply leave the fork fields (`entrainment`,
`custom_visual_pattern`, `animation_buffer_size`, `draw_depth`, `eye_spacing`) unset. No
sniffing, no second schema.

Pipeline:
1. `TextFormat::ParseFromString` with the fork descriptor.
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
   zero-weight entries validate already prunes, `renderer: OCULUS` → `"monitor"` with a
   printed warning, the entire `SessionArchive` message (dead: `export_archive` is a stub;
   the zip archive wave replaces it), deprecated fields 100/101 (migrated above).
6. `system.cfg` → `system.json`: mechanical; `last_session_map` keys (old `.session`
   paths) are copied as-is — stale keys are a harmless convenience-cache miss.

**Acceptance test:** convert → load the JSON (with pattern re-inlining) → compare the
resulting `trance_pb::Session` against `validate_session(load_proto(original))` using
`MessageDifferencer` with float tolerance 1/255 on colour components. Any other diff is a
converter bug.

**Lossy by design:** `#` comments in hand-edited textproto files are not carried over
(TextFormat discards them at parse). Re-add them as `_note` keys after converting.

## 8. Loader dispatch and the ImGui editor

**Playback and the editor load only `*.session.json` / `system.json`.** The legacy
textproto parser survives solely inside `trance_convert`; there is no dual load path and no
extension sniffing in the player. Constants change: `DEFAULT_SESSION_PATH =
"default.session.json"`, `SYSTEM_CONFIG_PATH = "system.json"` (`common.h:6-7`). A missing
`system.json` is regenerated from `get_default_system()` and saved, exactly as today
(`main.cpp:563-570`).

Load pipeline: parse JSON (strict per §2) → populate `trance_pb` → resolve pattern
`file`s into `source_text` + build sidecar → expand theme `scan` dirs → run
`validate_session()` / `validate_system()` unchanged. Everything downstream — playlist VM
in main.cpp, director, theme_bank, audio — consumes `trance_pb` objects and is untouched.

**What the ImGui editor reads/writes:**
- Reads/writes `*.session.json` through the same load/save layer (proto + sidecar). It is
  the only component that writes sessions; playback never does.
- Edits pattern text in a multiline widget; save writes the text to the pattern's `file`
  (§5), not into the JSON.
- Reads/writes `system.json` for its own state: `last_root_directory` (file dialogs),
  `last_export_settings` (export dialog defaults), `last_session_map` (pre-filling the
  launch variable picker, keyed by session path).
- Surfaces loader strict-mode errors (unknown key, bad path, missing pattern file,
  duplicate pattern name, both-oneof) with their JSON paths, and director parse warnings,
  in the UI rather than the console.

## 9. Named future steps (plan of record, not this wave)

1. **Archive wave:** `*.trance` = zip of the session root, session file stored as
   `session.json` at zip root; member names are the root-relative reference strings, so
   the §1 path contract makes packing a plain `zip -r`. Replaces the dead
   `SessionArchive` proto (offset/length table is subsumed by the zip central directory).
   `scan` themes resolve against zip contents.
2. **Theme audio (#23):** lands as an additive `audio_path` array on themes (grammar-first
   `audio` effect owns when/volume). Additive key — no `format_version` bump. Reserved
   here so nothing else claims the name.
3. **trance_pb retirement wave:** native in-memory structs replace `trance_pb`; the §5
   sidecar dissolves into real fields; comment preservation across editor saves becomes
   possible. The spec in this file does not change shape at that point — that is the test
   of the retirement wave.

## 10. Migration notes for existing sessions

1. Run `trance_convert my.session` once per session; a `my.session.json` and a
   `patterns/` dir appear beside it. Media files are not moved — relative paths were
   already root-relative and copy through verbatim. Keep or delete the old `.session`
   afterward; the player no longer reads it.
2. `system.cfg`: either convert it (preserves cache sizes, export defaults, variable
   memory) or just delete it — a fresh `system.json` regenerates with defaults on next
   launch.
3. Custom patterns previously embedded in the `.session` become `patterns/*.pattern`
   files named by pattern-name slug; edit those files directly from now on.
4. Sessions whose colours were hand-set to non-1/255-grid floats quantize to 8-bit on
   convert (visually identical; noted for round-trip exactness only).
5. `renderer: OCULUS` in an old `system.cfg` converts to `"monitor"` (already its
   effective behavior).
6. Any `#` comments in hand-edited textproto files are lost by the converter — re-add as
   `"_note"` keys.
7. Old launcher `.bat`/shortcut lines pointing at `.session` paths need the new
   `.session.json` path; `last_session_map` entries keyed by old paths simply stop
   matching (cosmetic — re-picking variables once repopulates them).
