# Command channel reference

This describes implemented controls. The filename is retained for existing links.
The application binds one session at startup; commands control that running
session. There is no live session-swap or command choreography engine.

## 1. Transports

`--command_port=9191` opens TCP on `127.0.0.1`. Default 0 disables the socket.
Each command is a UTF-8 line; each reply is one line:

```text
status
ok visual=super_fast bed=on muted=off overlay=off hidden=off uptime=42 xr=unattached themes=prev|primary|secondary|next themepin=- text=theme
overlay opacity 0.6
ok
unknown
err unknown verb: unknown
```

Replies start with `ok` or `err`; successful commands may include a message or
fields. Free-text names are not JSON-escaped, so treat status as a lightweight
diagnostic format, not a fully general serialization of arbitrary theme names.
The socket has no authentication or encryption; loopback is its access boundary.

`--mcp` provides newline-delimited JSON-RPC over stdin/stdout in the same binary.
It can coexist with the socket. Logs go to stderr and stdin EOF ends the run.
See [MCP setup and mapping](mcp-install.md).

## 2. Execution and persistence

Transport readers queue commands. The main/render thread drains each queue,
parses and applies commands, then returns replies. Transport threads do not
mutate playback state or call OpenGL.

Theme, text, and visual forces are runtime overrides and do not rewrite session
settings. Bed commands edit the active program in memory but do not initiate
autosave. **A later F2 save can persist those bed edits**, because it saves the
same live program.

## 3. Shared state rules

`hide` and `show` are idempotent. Hide makes the window invisible, pauses all
playback, mutes audio, and clears overlay. Show restores requested pause/mute
states; explicit pause/resume and mute commands while hidden update those
requests. Shift+F11 and the tray use the same hidden state.

Pause freezes desktop content while leaving UI available. With XR attached,
pause/hide submits no visual layers while continuing the frame handshake.
Physical-headset behavior remains subject to [XR acceptance](spec-xr-unified.md).

A theme pin must wait for asynchronous media loading. One runtime-pinned theme
occupies both live slots, two form the pair, and three or more supply candidates
for those two slots. F2 shows a runtime solo banner. Text pin replaces the text
source but adds no text draw to a visual that does not already have one.

## 4. Commands

| Command | Effect / arguments |
|---|---|
| `pause`, `resume` | Freeze / resume playback. |
| `hide`, `show` | Hide and silence / restore requested state. |
| `status` | Current visual, bed, mute, overlay, hidden, uptime, XR, theme slots, runtime content pins. |
| `overlay on`, `overlay off` | Enable / disable click-through overlay. |
| `overlay opacity VALUE` | Whole-window opacity, clamped 0–1. |
| `themes` | Theme entries with `name:weight`; `*` marks solo, `+` live, `!` no drawable content. |
| `theme pin NAME[,NAME...]` | Set the runtime theme solo set. |
| `theme unpin` | Restore the program's theme selection. |
| `visuals` | Built-ins/customs with weights, playing marker, and `forced=yes|no`. |
| `visual NAME` | Force a built-in or active-program custom pattern. |
| `load pattern FILE` | Read and force a v3 file on the trance machine. |
| `load pattern source SOURCE` | Compile and force source supplied in the command. |
| `unload pattern` | Release the visual force; use session pins/weights again. |
| `text pin TEXT[,TEXT...]` | Round-robin replacement text pool for all text draws. |
| `text unpin` | Restore theme text pools. |
| `ui on`, `ui off` | Open / close F2. On also shows the window and clears overlay. |
| `screenshot FILE` | Capture the next composited desktop frame, including UI, as PNG. |
| `mute on`, `mute off` | Global audio mute. |
| `bed on`, `bed off` | Enable bed (seed default layers if absent) / remove its layers. |
| `bed layers` | Read count, master level, and each layer's values. |
| `bed master DB` | Set bed master, clamped −60 to −6 dB; requires an active bed. |
| `bed layer add` | Append a 200 Hz carrier, 3 Hz binaural, continuous, −6 dB layer; return new count. |
| `bed layer remove I` | Remove zero-based layer I; return new count. |
| `bed layer I FIELD VALUE` | Set a field using the ranges below. |

| Layer field | Range |
|---|---|
| `carrier` | 20–1000 Hz |
| `binaural` | 0–40 Hz; zero disables split |
| `pulse` | 0–40 Hz; zero disables gate |
| `level` | −24–0 dB relative balance |

Layer balance is relative to the normalized mix; master controls absolute bed
level. Use `bed off` or mute for silence. Live bed edits glide, with stream
buffering latency. See [audio](audio.md).

Names and file/source arguments consume the rest of their line, preserving
internal spaces; do not add shell-style quotes inside a protocol command.
Theme/text lists use commas as separators. Inline source may use literal
`\n` sequences for line breaks, including after `#` comments.
Invalid pattern source reports a diagnostic and leaves the playing visual alone.

`ui` needs an initialized ImGui backend. Screenshots use the desktop render pass,
including when a headset is attached. File paths are resolved on the machine
running trance.

Unsupported commands include `start`, `stop`, `load session`, `intensity`,
`set`, and `get`; they have no implemented state transition.

## 5. Status

```text
ok visual=<name> bed=<on|off> muted=<on|off> overlay=<on|off> hidden=<on|off> uptime=<seconds> xr=<off|unattached|attached|attached-idle> themes=<a|b|c|d> themepin=<names|-> text=<theme|pinned:N>
```

Theme positions are unloading/previous, primary, secondary, and loading-next.
`off` includes unsupported XR platforms and a disabled probe after watchdog
timeout. `attached-idle` has an XR output without an actively running session.

## 6. Validation and source

[qa_command_channel.py](../tests/qa_command_channel.py) drives a live process
over the socket. It is a desktop QA harness, not a CTest target.

Implementation:
[command_channel.cpp](../src/trance/net/command_channel.cpp),
[command_protocol.cpp](../src/trance/net/command_protocol.cpp),
[mcp_stdio.cpp](../src/trance/net/mcp_stdio.cpp),
[main.cpp](../src/trance/main.cpp).