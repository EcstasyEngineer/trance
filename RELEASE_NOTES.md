# trance v4.0

V4 replaces the visual authoring language with a JSON abstract syntax tree shared
by the native player and visual composer. There is no V3 translation step or V3
execution option in the product. Existing custom V3 pattern files must be rewritten;
session themes, programs, playlists and audio settings remain separate.

- Three composable modes: parallel, sequential and random. Timing, cadence, ramps,
  offsets, repetition and chance exits are explicit properties.
- Six visual outputs: image, word, line, caption, subtext and spiral. Every image
  owns its held capture; authored registers and copy/draw bookkeeping are gone.
- All eight built-ins use V4. Parallel has two image lanes; Super Parallel has
  three. Flash Text uses two offset dissolve lanes. Its startup is opaque and
  both lanes refresh with current themes.
- Animation and still fallback share one theme-side selection. Animation rendering
  follows the output's captured side. Same-side decoders and text channels remain
  shared playback resources.
- The drag-and-drop composer edits the AST, supports import/export and exposes
  native compiler validation when served locally. The hosted composer runs without
  a local player; downloaded patterns can be checked with `trance --lint`.
- Eight synthesized audio-bed presets are selectable from F2 and command/MCP
  controls: bimbo-drone, reactor, descent, dreamy, celestial, warm, floating and
  sacred.

Download and extract the Windows release, then run `trance.exe` with a session
file or media in the working directory. See the [README](README.md),
[V4 grammar](docs/spec-grammar-v4.md) and [controls](docs/controls.md).

Native regression traces cover scheduler and output behavior; they do not
establish final decoded-media pixels or hardware timing. Physical OpenXR headset
acceptance remains pending. See [validation coverage](docs/architecture-maturity.md).
