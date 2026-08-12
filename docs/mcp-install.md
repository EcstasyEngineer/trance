# MCP over stdio (`--mcp`) — beta

Trance can serve **MCP (Model Context Protocol) on its own stdin/stdout**: an MCP host
(Claude Desktop, Claude Code, or any MCP client) launches `trance --mcp` directly and
drives playback through tool calls. There is no sidecar process, no Python, and no
socket — the binary itself is the server, and the host owns its lifecycle (trance exits
when the host closes its stdin).

The tools map **1:1 onto the command-channel verbs**
([spec-mcp-ambient-daemon.md](spec-mcp-ambient-daemon.md) §4); on the execute side an
MCP tool call is indistinguishable from a line typed at the loopback socket. `--mcp` and
`--command_port` are independent and can run together.

## Registering with Claude Code

```sh
claude mcp add trance -- "C:\path\to\trance.exe" "C:\path\to\media\session.json" --mcp --hidden
```

The session argument is optional, exactly as on a normal launch — without it trance
generates a default session from the media in its working directory.

`--hidden` is strongly recommended and is why it exists: the host **launches** the process
when it connects, so without it connecting the server puts the fullscreen player on screen
before any tool call could hide it. With it the process comes up in silent running — window
never mapped, playback paused, audio muted — and `show` is what makes it visible. Add
`--muted` too if a later `show` should bring up a visible-but-silent player (`--hidden`'s
mute is released by `show`; `--muted` survives it).

## Registering with Claude Desktop

Add to `claude_desktop_config.json` (Settings → Developer → Edit Config):

```json
{
  "mcpServers": {
    "trance": {
      "command": "C:\\path\\to\\trance.exe",
      "args": ["C:\\path\\to\\media\\session.json", "--mcp", "--hidden"]
    }
  }
}
```

## Tools

| Tool | Arguments | Effect |
|---|---|---|
| `status` | — | One-line status: current visual, bed, muted, overlay, hidden, uptime, headset, the four theme slots, and whether a theme/text pin is in effect. |
| `pause` / `resume` | — | Freeze / unfreeze playback in place. |
| `hide` / `show` | — | Silent running: window invisible, playback paused, audio muted, process alive. Idempotent. |
| `overlay_on` / `overlay_off` | — | Click-through overlay mode on the running window. |
| `overlay_opacity` | `opacity` 0..1 | Overlay opacity (clamped). |
| `themes` | — | List every theme in the session: `name:weight` with `*` pinned, `+` live now, `!` nothing to draw. |
| `theme_pin` | `names` (1–2, comma-separated) | Hold the session on those themes. One name → both theme slots; two → one each. Takes a few seconds to load in. |
| `theme_unpin` | — | Back to the program's theme rotation. |
| `visuals` | — | List the built-ins and the active program's custom patterns (`name:weight`, `*` = playing), plus `forced=yes/no`. |
| `visual` | `name` | Force every visual selection to that built-in or custom pattern. |
| `load_pattern` | `file` | Load and pin a v3 pattern from a source file **on the trance machine**. |
| `load_pattern_source` | `source` | Load and pin a v3 pattern from source text sent over the connection — no file needed. |
| `unload_pattern` | — | Release any pinned visual (`visual`, `load_pattern`, `load_pattern_source`) and resume the program's schedule. |
| `text_pin` | `words` (comma-separated) | Every text draw serves from these words, round-robin, instead of the themes' text. |
| `text_unpin` | — | Back to the themes' own text. |
| `ui_on` / `ui_off` | — | Open / close the F2 control panel remotely. |
| `screenshot` | `file` | Dump the next rendered frame to a PNG on the trance machine. |
| `mute_on` / `mute_off` | — | Global audio mute (the M key's toggle). |
| `bed_on` / `bed_off` | — | Entrainment bed on/off. |
| `bed_layers` | — | **Read** the bed back: layer count, master, and each layer's carrier/binaural/pulse/level. |
| `bed_master` | `db` | Bed master level in dB (clamped −60..−6). The bed's absolute volume. |
| `bed_layer_add` | — | Append an entrainment layer. Returns `layers=N` (the count *after* the append). |
| `bed_layer_remove` | `index` | Remove the layer at `index`. Returns `layers=N` (the count *after* the removal). |
| `bed_layer_set` | `index`, `field` (`carrier`\|`binaural`\|`pulse`\|`level`), `value` | Set one layer field; the reconfigure morphs live. |

`hide` is the one to know: it is the instant off-switch (screen share starting, someone
walks in) — one tool call, no process kill, instant restore with `show`.

### Things a controller gets wrong without being told

Each of these cost real diagnostic time in a live session (#60):

- **A layer emits a binaural beat *and* an isochronic pulse, simultaneously.** They are
  independent rates from the same layer and both are audible. Sweeping one layer's binaural
  from 10 → 4 Hz while its pulse sits at 4 therefore produces two perceived rates, which
  reads convincingly as "the layer I removed is still sounding". It isn't.
- **A layer's `level` is a relative balance, not a volume.** The bed normalises its summed
  level to the master (`EntrainmentStream::Configure` measures the mix's RMS/peak and
  derives the master gain from it), so dropping the *only* layer to −60 dB just raises the
  master gain to compensate: no silence, and a brief swell while the two glides cross. For
  absolute level use `bed_master`; for silence use `bed_off` or `mute_on`.
- **`bed_layer_remove` really removes.** A later `bed_layer_set` on that index replies
  `err bed layer: no layer 1 (bed has 1)`. Read `bed_layers` first if you might want it back.
- **A pin is not instant.** `theme_pin` has to wait for the bank to load the theme in, so a
  screenshot taken immediately still shows the old theme. That is loading, not failure.
- **Pinned text only shows where a visual draws text.** `text_pin` overrides the text
  *source*; it does not add text draws. Pair it with `visual sub_text` / `visual flash_text`
  if nothing appears.
- **Runtime pins do not edit the session file.** `theme_pin`, `text_pin` and the visual
  forces are process-lifetime state; the session JSON on disk is untouched.

## Notes / limitations (beta)

- **stdout is the transport.** In `--mcp` mode every log line the app would print moves
  to stderr; the host's MCP logs show them.
- File-path arguments (`load_pattern`, `screenshot`) are paths **on the machine running
  trance**, resolved by the trance process. `load_pattern_source` exists precisely so a
  controller that is not that machine can still author a pattern.
- Trance exits when the host closes stdin (the MCP convention). Quitting trance from its
  own UI while the host still holds the connection reads to the host as the server
  dying — expected, but the host may log it as an error.
- Trust model is unchanged from the command channel: whoever can launch the process
  controls playback. There is no auth layer; do not wrap the stdio transport in anything
  network-facing.
