# MCP setup

trance's `--mcp` option serves tools over stdin/stdout. An MCP host launches
the binary and owns its lifetime; closing stdin ends playback and exits.
No bridge process is required. The tools use the same execution path as the
[command channel](spec-mcp-ambient-daemon.md).

## Launch configuration

Configure a stdio server in your host with an absolute executable path and
arguments such as:

```json
{
  "command": "C:\\trance\\trance.exe",
  "args": ["C:\\media\\my.session.json", "--mcp", "--hidden"]
}
```

Place that server definition inside the host's own configuration structure.
An explicit session path avoids depending on the host's working directory.
Without it, trance uses or creates `./default.json` there.

`--hidden` starts with no visible window, paused playback, and muted audio.
Call `show` to reveal and restore it. Add `--muted` if showing should remain
silent. Without `--hidden`, connection starts the visible player immediately.

stdout carries JSON-RPC only; diagnostic logs go to stderr. A quit from trance's
own UI ends the server connection. `--command_port` can run alongside MCP.

## Tool mapping

Semantics and clamping are defined once in the
[command reference](spec-mcp-ambient-daemon.md#4-commands).
Arguments below are JSON object fields; tools with none accept an empty object.

| MCP tool | Arguments | Command |
|---|---|---|
| `status` | — | `status` |
| `pause`, `resume` | — | `pause`, `resume` |
| `hide`, `show` | — | `hide`, `show` |
| `overlay_on`, `overlay_off` | — | `overlay on|off` |
| `overlay_opacity` | `opacity`: number | `overlay opacity VALUE` |
| `themes` | — | `themes` |
| `theme_pin` | `names`: comma-separated string | `theme pin NAMES` |
| `theme_unpin` | — | `theme unpin` |
| `visuals` | — | `visuals` |
| `visual` | `name`: string | `visual NAME` |
| `load_pattern` | `file`: string | `load pattern FILE` |
| `load_pattern_source` | `source`: string | `load pattern source SOURCE` |
| `unload_pattern` | — | `unload pattern` |
| `text_pin` | `words`: comma-separated string | `text pin WORDS` |
| `text_unpin` | — | `text unpin` |
| `ui_on`, `ui_off` | — | `ui on|off` |
| `screenshot` | `file`: string | `screenshot FILE` |
| `mute_on`, `mute_off` | — | `mute on|off` |
| `bed_on`, `bed_off` | — | `bed on|off` |
| `bed_layers` | — | `bed layers` |
| `bed_master` | `db`: number | `bed master DB` |
| `bed_layer_add` | — | `bed layer add` |
| `bed_layer_remove` | `index`: integer | `bed layer remove I` |
| `bed_layer_set` | `index`: integer, `field`: string, `value`: number | `bed layer I FIELD VALUE` |

## Controller behavior

Read `status`, `themes`, `visuals`, or `bed_layers` before changing state.
Wait for a theme pin to load before judging its screenshot. Text pins supply
content only where the current visual already draws text.

`load_pattern` and `screenshot` paths are on the trance machine.
`load_pattern_source` sends the source directly and is suitable when the
controller cannot place files there.

Theme/text/visual pins do not edit the session file. Bed edits mutate the live
program without initiating a save; a subsequent F2 save can persist them.
For bed volume, use master level; a layer's level is relative mix balance.

MCP is still labeled beta in the executable. Its stdio implementation is intended
for a local host, and the optional TCP transport is loopback-only.