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
claude mcp add trance -- "C:\path\to\trance.exe" "C:\path\to\media\session.json" --mcp
```

The session argument is optional, exactly as on a normal launch — without it trance
generates a default session from the media in its working directory.

## Registering with Claude Desktop

Add to `claude_desktop_config.json` (Settings → Developer → Edit Config):

```json
{
  "mcpServers": {
    "trance": {
      "command": "C:\\path\\to\\trance.exe",
      "args": ["C:\\path\\to\\media\\session.json", "--mcp"]
    }
  }
}
```

Note that the host **launches** trance: connecting the server starts the fullscreen
player. Keep it disabled/disconnected until you mean it, or pair it with an immediate
`hide` tool call.

## Tools

| Tool | Arguments | Effect |
|---|---|---|
| `status` | — | One-line status: playing/paused, current visual, overlay, hidden, bed, themes. |
| `pause` / `resume` | — | Freeze / unfreeze playback in place. |
| `hide` / `show` | — | Silent running: window invisible, playback paused, audio muted, process alive. Idempotent. |
| `overlay_on` / `overlay_off` | — | Click-through overlay mode on the running window. |
| `overlay_opacity` | `opacity` 0..1 | Overlay opacity (clamped). |
| `load_pattern` | `file` | Load and pin a v3 pattern from a source file on the trance machine. |
| `ui_on` / `ui_off` | — | Open / close the F2 control panel remotely. |
| `screenshot` | `file` | Dump the next rendered frame to a PNG on the trance machine. |
| `mute_on` / `mute_off` | — | Global audio mute (the M key's toggle). |
| `bed_on` / `bed_off` | — | Entrainment bed on/off. |
| `bed_master` | `db` | Bed master level in dB (clamped to the F2 slider range). |
| `bed_layer_add` | — | Append an entrainment layer. |
| `bed_layer_remove` | `index` | Remove the layer at `index`. |
| `bed_layer_set` | `index`, `field` (`carrier`\|`binaural`\|`pulse`\|`level`), `value` | Set one layer field; the reconfigure morphs live. |

`hide` is the one to know: it is the instant off-switch (screen share starting, someone
walks in) — one tool call, no process kill, instant restore with `show`.

## Notes / limitations (beta)

- **stdout is the transport.** In `--mcp` mode every log line the app would print moves
  to stderr; the host's MCP logs show them.
- File-path arguments (`load_pattern`, `screenshot`) are paths **on the machine running
  trance**, resolved by the trance process.
- Trance exits when the host closes stdin (the MCP convention). Quitting trance from its
  own UI while the host still holds the connection reads to the host as the server
  dying — expected, but the host may log it as an error.
- Trust model is unchanged from the command channel: whoever can launch the process
  controls playback. There is no auth layer; do not wrap the stdio transport in anything
  network-facing.
