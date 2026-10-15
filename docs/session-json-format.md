# Session JSON format

Current session and system files use JSON with `format_version: 1`.
The loader/saver is [session_json.cpp](../src/common/session_json.cpp);
[session.cpp](../src/common/session.cpp) performs validation and legacy dispatch.
The in-memory model remains [trance.proto](../src/common/trance.proto), with a
sidecar for pattern paths and scan metadata. This reference describes implemented
behavior, including limitations; future model replacement is not assumed.

## 1. Files and paths

| File | Purpose |
|---|---|
| `*.session.json` | Session; any `.json` basename is accepted. |
| `./default.json` | Default session, created from the working directory on a no-argument cold start. |
| `./system.json` | Default machine settings path; an optional second positional argument selects another. |
| `*.pattern` | Plain-text v3 pattern source, referenced by the session. |
| `*.trance` | Exported standard ZIP with `session.json` at its root. |

The session file's parent is the media root. Media, audio, pattern, and scan paths
are relative to that root. Use forward slashes and no `..` segments or absolute
paths. The loader rejects host-recognized absolute paths and `..` segments;
the saver normalizes backslashes. This is a portability convention and path
validation, not a filesystem sandbox.

A malformed explicitly named session or existing default fails instead of being
overwritten with generated content.

## 2. Encoding

- Top-level discriminators are `"trance-session"` and `"trance-system"`.
  Write integer `format_version: 1`. The current loader rejects versions above 1;
  it does not reject smaller integers.
- Keys use the field names below. Unknown non-comment keys are errors.
- Keys starting with `_` are ignored, including in named maps. Avoid using such
  names for programs, themes, playlist items, or variables.
- Enums are lowercase strings. JSON uses `parallel`; CLI `--visual` calls that
  same built-in `simple`.
- Colors are `"#RRGGBB"` or `"#RRGGBBAA"`. Missing alpha means FF. Saving converts
  float components to rounded/clamped bytes.
- Omitted fields initially have protobuf defaults: zero, false, empty string,
  or empty collections. Validation then repairs essential defaults and ranges.
  Omission does not always select the generated default-program value.
- Writers omit default-valued fields and use two-space indentation. Comment keys
  and hand-chosen key ordering do not survive saving.

## 3. Session schema

| Key | Type / meaning |
|---|---|
| `format` | `"trance-session"` |
| `format_version` | Integer; write 1 |
| `first_playlist_item` | Playlist entry name |
| `playlist` | Object mapping name to playlist item |
| `program_map` | Object mapping name to program |
| `theme_map` | Object mapping name to theme |
| `theme_scan_root` | Directory string or `{ "dir": ".", "auto_rescan": false }` |
| `variable_map` | Object mapping name to variable |

### Theme discovery

An omitted or empty scan root becomes `"."`; automatic discovery defaults to
true. Thus an omitted `theme_scan_root` does **not** freeze the theme list.
Use the object form with `auto_rescan: false` to stop new folders joining.
Existing themes' own scans still read new files.

Discovery adds one theme per directory containing direct content, with a weight-1
row in each program when that row does not already exist. Loose files at the scan
root form `/root/`. Missing directories do not delete themes or their settings.
This avoids losing exclusions and weights for temporarily unavailable media.
New themes are discovered on load, not continuously during playback.

### 3.1 Playlist items

Exactly one of `standard` and `subroutine` must be present.

| Key | Type / meaning |
|---|---|
| `standard` | Object |
| `standard.program` | Program name |
| `standard.play_time_seconds` | Unsigned seconds; zero allows immediate branching |
| `subroutine` | Array of playlist item names, run in order |
| `next_item` | Array of branch objects |
| `next_item[].playlist_item_name` | Destination name |
| `next_item[].random_weight` | Unsigned selection weight |
| `next_item[].condition_variable_name` | Optional variable name |
| `next_item[].condition_variable_value` | Required with the condition name; both keys or neither |
| `audio_event` | Array of audio event objects |
| `audio_event[].type` | `"play"`, `"stop"`, or `"fade"` |
| `audio_event[].channel` | Unsigned channel index |
| `audio_event[].next_unused_channel` | Boolean; play searches upward for a non-playing channel |
| `audio_event[].path` | Root-relative audio file |
| `audio_event[].loop` | Boolean; used by play |
| `audio_event[].volume` | Unsigned 0–100; play initial / fade target |
| `audio_event[].time_seconds` | Unsigned fade duration |

A top-level item with no eligible branch holds indefinitely. A subroutine child
with no branch returns. Zero-duration cycles can hang the runner; see
[playlist behavior](sessions-and-playlists.md#playlist-flow).
Deprecated flat item-level `program` and `play_time_seconds` are rejected.

### 3.2 Programs

| Key | Type / meaning |
|---|---|
| `enabled_theme` | Array |
| `enabled_theme[].theme_name` | Theme name |
| `enabled_theme[].random_weight` | Unsigned rotation weight |
| `enabled_theme[].pinned` | Boolean; saved theme solo set |
| `visual_type` | Array |
| `visual_type[].type` | `accelerate`, `slow_flash`, `sub_text`, `flash_text`, `parallel`, `super_parallel`, `animation`, `super_fast` |
| `visual_type[].random_weight` | Unsigned selection weight |
| `visual_type[].pinned` | Boolean; saved visual solo set |
| `custom_visual_pattern` | Array |
| `custom_visual_pattern[].name` | Identity; unique within the program |
| `custom_visual_pattern[].file` | Root-relative pattern source path |
| `custom_visual_pattern[].random_weight` | Unsigned selection weight |
| `custom_visual_pattern[].enabled` | Boolean; omitted means disabled |
| `custom_visual_pattern[].pinned` | Boolean; cleared if disabled |
| `global_fps` | Unsigned content tick rate; validation clamps 1–240 |
| `zoom_intensity` | Number; validation clamps 0–1 |
| `spiral_colour_a`, `spiral_colour_b` | Hex colors |
| `reverse_spiral_direction` | Boolean |
| `main_text_colour`, `shadow_text_colour` | Hex colors |
| `entrainment` | Object; absent means no bed |
| `entrainment.master_db` | Number; zero/omitted selects −28 dB |
| `entrainment.layer` | Array |
| `entrainment.layer[].center_hz` | Carrier frequency |
| `entrainment.layer[].binaural_hz` | Left/right frequency difference; zero disables split |
| `entrainment.layer[].pulse_hz` | Pulse rate; zero gives continuous carrier |
| `entrainment.layer[].amplitude_db` | Relative layer balance; zero is unity |

Weights are ticket counts, not percentages. Enabled built-ins and custom
patterns share a visual pool. Visual pins restrict selection to the pinned set;
runtime/CLI forcing takes precedence.

Saved theme pins: one keeps a theme resident with a weighted partner, two select
the pair, and three or more form the candidate set for two live slots.
A one-name runtime `theme pin` instead occupies both slots because the override
removes other rotation candidates.

Validation restores all themes at weight 1 when no usable theme weight or pin
exists. Visual fallback specifically checks built-in weights and enabled pins:
custom weights alone do not suppress default built-ins. Invalid custom source
may still be skipped by the director.

Inherited image selection picks a source tier by that source theme's rotation
weight, then an image in the tier. A source without positive rotation weight
gets tier weight 1. File counts do not determine relative tier share. Loading
uses the same weighting. Animations, text, and audio use flat merged pools.

### 3.3 Themes

| Key | Type / meaning |
|---|---|
| `scan` | Directory string or scan object |
| `scan.dir` | Required directory in the object form |
| `scan.inherit` | Boolean, default false |
| `scan.exclude` | Array of root-relative file paths held out of the theme's own scan |
| `image_path` | Array of root-relative image paths |
| `animation_path` | Array of root-relative animation paths |
| `font_path` | Array of root-relative font paths |
| `text_line` | Array of strings; embedded newlines are allowed |
| `audio_path` | Array of root-relative recorded-audio paths |

A scan reads direct files only. One directory is one theme.
`inherit` folds in the nearest ancestor directory's theme; pure containers are
skipped. It continues transitively when that ancestor inherits. It never adds
descendants or siblings. Exclusions apply at the owning theme, not separately
to inherited files, and are retained even when the file is absent.

Explicit lists load too. When an unscanned theme has a matching nonempty directory
by theme name, the loader replaces its lists with that directory's scan. Missing
or empty matching directories leave the lists intact. Omitted files in old lists
do not become exclusions during this conversion.

Combining explicit entries with `scan` appends scan results at load, but saving
a scanned theme emits only its scan metadata. Explicit extras beside `scan`
therefore do not survive an F2 save.

| Scanned extension | Destination |
|---|---|
| `gif`, `webm` | Animations |
| `ttf` | Fonts |
| `txt` | Nonempty lines, uppercased and split near the middle space |
| `wav`, `flac`, `ogg`, `aiff`, `mp3` | Audio |
| Other eligible extensions | Images; decode failures are excluded from drawing |

Scans skip dotfiles/dotted directories, extensionless files, `thumbs.db`,
session/config/pattern/archive files, INI/log files, and common backup/partial
suffixes (`bak`, `tmp`, `swp`, `part`, `crdownload`, trailing `~`).
Markdown notes and unsupported video containers (`mp4`, `mov`, `m4v`, `mkv`,
`avi`, `wmv`, `mpg`, `mpeg`) are also skipped; they do not become still images.
Failed still-image decodes do not occupy the cache or count as loaded images.
The exact classifier is `is_scan_ignored` in `session.cpp`.

### 3.4 Variables

| Key | Type / meaning |
|---|---|
| `description` | String metadata |
| `value` | Array of allowed strings |
| `default_value` | String; validation repairs it to a listed value |

**Runtime limitation:** the current player supplies playlist values from
`--variables`. It does not seed them from `default_value` or the stored
`last_session_map`; an absent variable compares as an empty string.
There is no implemented F2 launch-variable picker.

### 3.5 Minimal example

With a `media` folder beside the file:

```json
{
  "format": "trance-session",
  "format_version": 1,
  "theme_scan_root": { "dir": ".", "auto_rescan": false },
  "first_playlist_item": "main",
  "playlist": {
    "main": { "standard": { "program": "default" } }
  },
  "program_map": {
    "default": {
      "global_fps": 120,
      "enabled_theme": [{ "theme_name": "media", "random_weight": 1 }],
      "visual_type": [{ "type": "slow_flash", "random_weight": 1 }]
    }
  },
  "theme_map": { "media": { "scan": "media" } }
}
```

This intentionally has no audio bed. Add colors, zoom, and other program fields
as needed; omitted numeric/color settings use validated protobuf defaults.

## 4. Pattern files

Custom patterns use external plain-text v3 files. JSON has no inline
`source_text` key. Identity comes from `name`, not the filename, and multiple
entries may reference the same file.

A missing/unreadable file fails session loading. Invalid source syntax is a
different error: the director skips that custom visual and surfaces a warning.
See [authoring](authoring-v3-patterns.md) and [grammar](spec-grammar-v3.md).

## 5. Saving

`SessionJsonSidecar` records pattern paths, scan directories, inherit/exclude
settings, scan-root options, and derived image-tier metadata. The saver writes
pattern text back to its known path. New patterns get `patterns/<slug>.pattern`;
slugs lowercase names, keep `[a-z0-9_-]`, replace other characters with `_`,
and suffix generated collisions with `-2`, `-3`, etc.

F2 autosaves committed edits to the loaded session. Export writes a copy.
Session/system JSON writes use a sibling `.tmp` and rename, preventing a
partially written JSON file from replacing the old one. Pattern files are written
separately; this is not an atomic transaction across a session and all its assets.

Saving discards comment keys, original formatting, and explicit additions beside
a scan. Keep a separate authored copy if those need preservation.
Command-channel bed edits do not initiate saving, but a later F2 save includes
the live program. Runtime theme/text/visual forces are separate overrides.

## 6. System schema

The wrapper is `{ "format": "trance-system", "format_version": 1, ... }`.

| Key | Type / meaning |
|---|---|
| `enable_vsync` | Boolean |
| `windowed` | Boolean; changes apply next launch |
| `draw_depth` | Number, clamped 0–1; absent defaults to 0.5 |
| `eye_spacing` | Number, clamped −1–1; absent defaults to 0.0625 |
| `image_cache_size` | Unsigned, minimum 16 |
| `animation_buffer_size` | Unsigned, minimum 8 |
| `font_cache_size` | Unsigned, minimum 2 |
| `last_root_directory` | String; retained metadata, absolute paths allowed |
| `last_session_map` | Object: session path → object: variable name → string value; retained metadata |
| `last_export_settings` | Retained object; fields below |

Retained export fields are `path` (string), `export_3d` (boolean), and unsigned
`width`, `height`, `fps`, `length`, `quality`, `threads`.
The current player has no video-export consumer for them. It also does not use
the retained root-directory/session-variable metadata as a launch dialog.

`draw_depth` and `eye_spacing` preserve presence: explicit zero differs from
omission. The obsolete `renderer` key is accepted and ignored; XR is automatic.
The next save drops that key while preserving other settings. An invalid system file causes
startup to attempt sibling legacy `system.cfg` migration, otherwise regenerate
defaults. Other settings can be lost in that recovery.

## 7. Legacy migration

Upstream text-protobuf `.session` input is parsed against the frozen
[legacy.proto](../src/common/legacy.proto) descriptor and translated by
[session_legacy.cpp](../src/common/session_legacy.cpp). The original file stays
intact; a same-stem JSON sibling is written and loaded. A supplied `old.session`
becomes `old.json`, not `old.session.json`.

`trance --lint old.session` performs this migration without a window.
A no-argument startup can migrate `default.session`; invalid/missing system JSON
can migrate sibling `system.cfg`.

The importer accepts the upstream schema, not fork-added fields such as
`entrainment`, custom patterns, or theme audio. Unsupported fields fail with
diagnostics. Deprecated theme names and flat playlist fields translate into the
current model. Legacy renderer selection is discarded. Comments and non-byte-grid
color precision are not preserved.

## 8. Validation boundaries

JSON checks types, keys, paths, pattern-file availability, duplicate pattern
names, and item shape. The subsequent repair pass fills empty maps, repairs
references/defaults, prunes invalid branches, and clamps selected fields.

It does not prove that every playlist terminates, every file decodes, every
custom pattern compiles, or every authored timing behaves as intended.
`trance --lint [session]` additionally checks pattern compilation/evaluation.
See [architecture maturity](architecture-maturity.md) for verification scope.

## 9. Archives

`trance --export_archive out.trance my.session.json` writes a ZIP containing
`session.json` and referenced theme media, pattern sources, and playlist audio
under root-relative paths. Entries are stored without recompression.
Extract using any ZIP tool, then launch the extracted session.

This is the implemented archive format; the old `SessionArchive` protobuf
message is unused.
