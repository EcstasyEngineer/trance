# Spec — Trance command channel (in-process, issue #21)

> **Rescoped 2026-07-01, per owner decision.** The previous drafts of this spec framed the
> daemon around agent-driven "conditioning moments" — a `Moment` proto, a `trigger()`
> primitive, choreographed timed sequences compiled to transient cycler trees. That framing
> is off base for this spec's scope. **The daemon is a dumb settings-shaped effector**: a
> small, fixed verb set that starts/stops/pauses playback, flips an overlay, nudges a global
> intensity knob, and gets/sets settings. It does not choreograph anything, does not know
> what a "moment" is, and does not read context. Composition and choreography — if anyone
> wants that — lives entirely in whatever connects to the socket. The in-process command
> channel decision (localhost TCP, C++ reader thread, no bridge, no auth beyond loopback
> binding) stands unchanged from the prior rescope; only the verb surface and its framing
> change here.

Trance becomes a **dumb effector**: a run mode whose playback is driven on demand by an
external controller over a **local socket**, using a **small, fixed verb set**. Trance senses
nothing and choreographs nothing; it exposes "do X now" / "set KEY to VALUE" verbs. Any
decision-making — including any agent — lives in whatever connects.

**Scope decisions (unchanged from the prior rescope):** the whole feature lives **in the C++
app**. There is **no separate bridge process** and **no Python** in the runtime — Python is
permitted *only* as a test client. The transport is **loopback TCP with no auth and no
encryption** (it binds `127.0.0.1` only; if you can open a socket to localhost you are already
on the machine). No token, no TLS, no schema negotiation, no streaming.

This spec is the buildable plan for **v0**.

---

## 1. Architecture (one process)

```
  controller (script / test / MCP server / anything)  --localhost TCP, line text-->  trance (C++)
                                                                                       |
                                             reader thread -> mutex queue -> render loop drains 1x/frame
```

Two things, each hangs off the prior, **both in the trance binary**:
1. **In-process command channel** (C++) — the keystone: a loopback socket + reader thread.
2. **Line-oriented plain-text protocol** — a fixed verb set, one command per line, one reply
   line per command.

No MCP SDK, no bridge, no auth layer, no choreography engine. If MCP-client integration is
ever wanted, it is a **separate, external thin MCP server** that maps MCP tools onto these
verbs (§8) — out of this repo's scope, not built here.

---

## 2. The command channel (C++) — `src/trance/net/command_channel.{h,cpp}`

The **inverse of the ThemeBank async-loader pattern**: there a worker loads while the render
thread reads atomics; here a worker *receives* while the render thread *drains a queue*. Same
threading discipline -> idiomatic.

```cpp
class CommandChannel {
public:
  // Binds 127.0.0.1:<port> and spawns the reader thread. Loopback only; no auth.
  explicit CommandChannel(uint16_t port);
  ~CommandChannel();                       // signals stop, joins the reader thread

  struct Command { uint64_t conn_id; std::string line; };
  // Render-thread only: move out everything received since the last call.
  std::vector<Command> drain();
  // Render-thread only: reply on the same connection (one line back per command).
  void reply(uint64_t conn_id, const std::string& line);

private:
  void reader_loop();                      // owns the socket; pushes raw lines into _queue
  std::mutex _mutex;
  std::vector<Command> _queue;             // guarded by _mutex
  std::atomic<bool> _running;
  std::thread _reader;
  // winsock on Windows (ws2_32, already linked by trance), BSD sockets on Linux.
};
```

**Threading invariant (load-bearing):** the reader thread only ever pushes raw lines into the
mutex-guarded queue. **`Director` is mutated only at the drain point on the render thread** —
never from the reader thread. This is exactly the ThemeBank discipline (worker side touches
only its own atomics; the render thread owns the mutations).

**Transport:** TCP on `127.0.0.1` only. No token, no TLS — loopback is the trust boundary.
Cross-platform, trivial to drive from a test, inspectable with `netcat`. Port comes from a
launch flag (`--listen <port>`), default off (no socket unless asked).

---

## 3. Protocol — line-oriented plain text

One command per line, one reply line per command. Deliberately not JSON: the verb set is
small and flat enough that plain text is simpler to parse on the C++ side and trivially
scriptable from the shell (`echo | nc`, no JSON library needed on the client end).

```
$ echo "status" | nc -q1 127.0.0.1 9191
ok visual=spiral_bloom bed=on overlay=off hidden=off uptime=142 themes=nature|nature|space|words

$ echo "intensity 0.6" | nc -q1 127.0.0.1 9191
ok

$ echo "set fps 30" | nc -q1 127.0.0.1 9191
ok

$ echo "get fps" | nc -q1 127.0.0.1 9191
ok fps=30

$ echo "frobnicate" | nc -q1 127.0.0.1 9191
err unknown verb: frobnicate
```

Reply grammar: `ok[ <space-separated key=value pairs>]` or `err <message>`. Always exactly
one line back per command line in. Unknown verb / malformed line -> `err ...`, never a crash,
never a dropped connection.

Parse + dispatch happens in the drain loop (`main.cpp`'s per-frame loop, right after
`handle_events`).

---

## 4. Verb set (v0 — this is the whole surface)

Playback lifecycle:
- **`start`** — begin/resume playback from a stopped state (loads nothing new; use with
  `load session` first if starting cold).
- **`stop`** — halt playback, return to idle.
- **`pause`** — freeze the current frame; program state retained.
- **`resume`** — un-freeze after `pause`.

Overlay (live, drives issue #27's click-through overlay on the running window):
- **`overlay on`** / **`overlay off`** — apply/clear the overlay hints at runtime: on makes
  the window click-through, translucent and always-on-top; off restores a normal window.
- **`overlay opacity VALUE`** — `VALUE` in `0..1`, clamped; applies live while the overlay
  is on.

Silent running (the hide-everything primitive — **this is the switch an MCP agent flips to
make trance vanish instantly without killing the process**):
- **`hide`** — hide everything: the window becomes invisible (`setVisible(false)`), playback
  pauses, audio mutes; the process — command channel, tray icon, Shift+F11 hotkey — stays
  alive. Idempotent: `hide` while already hidden is an `ok` no-op.
- **`show`** — restore: window visible again, and the pause/mute state that existed *before*
  hiding comes back (a session that was playing unmuted resumes playing unmuted; one that
  was already paused stays paused). A `pause`/`resume`/`start`/`stop` (or tray Paused
  toggle) issued *while* hidden updates that restored pause state instead of being
  discarded — playback stays idle for as long as the window is hidden, and on `show` the
  last explicitly commanded pause state wins. Idempotent like `hide`.
- Both verbs are available in **every** mode, including VR: the apply seam pauses and
  mutes without touching the headset's hidden GL-context helper window, so there is no
  configuration in which the intent goes unapplied. (An earlier revision made them reply
  `err ... unavailable in this mode (export)` under video export, on the grounds that an
  `ok` flipping nothing — with `status` reporting `hidden=on` forever — would be a lie.
  The video-export mode has since been removed, and with it the only mode that had no
  window/seam to apply these to.) Contrast `ui`/`screenshot` below, which remain
  capability-gated because VR genuinely has no flat pass to draw or grab.
- Same state everywhere: `hide`/`show`, the global **Shift+F11** hotkey, and the tray's
  Hide-everything/Show item all drive one `hidden` flag reconciled at the main loop's apply
  seam, so the surfaces can never disagree. Hiding also forces the overlay off (clearing
  the click-through hints), so no stuck click-through state can survive a hide/show cycle.
- **Shift+F11 semantics (revised):** the hotkey is now a pure hide/show *toggle* — first
  press hides everything instantly, next press restores. It no longer shows the control
  panel and no longer quits on a second press; quitting is the Escape key, the tray's Quit
  item, the window close button, or the F2 panel's Quit button. One carve-out: in hotkey-only
  configurations where none of those quit surfaces exist (Linux VR, or Linux fullscreen
  after a failed ImGui init — no tray, no panel), a press while already hidden quits
  instead of restoring, so an orderly exit always remains reachable.

Intensity:
- **`intensity VALUE`** — `VALUE` in `0..1`, clamped. A single global multiplier concept, not
  a per-effect control. Semantics are defined loosely on purpose: think "master zoom / alpha /
  spiral-speed scaling," turning the whole visual up or down as one knob. Exact wiring (which
  render params it scales, whether it's linear or curved) is **TBD at implementation time** —
  this spec fixes the verb and the `0..1` contract, not the internal formula.

Settings (generic get/set — **note:** the settings surface itself is moving from the protobuf
`Program`/`.session` shape to JSON later this sprint; key names are TBD by that migration and
deliberately not enumerated here):
- **`set KEY VALUE`** — set a settings key to a value; `ok` or `err unknown key: KEY`.
- **`get KEY`** — read a settings key back; `ok KEY=VALUE` or `err unknown key: KEY`.

Loading:
- **`load pattern FILE`** — load/compile a single v3 pattern file as the active visual.
- **`load session FILE`** — load a `.session` (or, post-migration, its JSON successor) as the
  active program.

Status:
- **`status`** — single-line, parseable reply: current visual name, entrainment-bed state,
  overlay state, hidden state, process uptime, and ThemeBank's four queue slots. Exact
  reply shape:
  `ok visual=<name> bed=<on|off> overlay=<on|off> hidden=<on|off> uptime=<seconds> themes=<a|b|c|d>`

Debug/validation (same line protocol, not part of the settings surface proper):
- **`ui on|off`** — show/hide the F2 ImGui panels remotely (same state the F2 key
  toggles; `ui on` also un-hides and disengages the overlay, since a panel on an invisible
  or click-through window is unreachable); `err ... unavailable in this mode (VR)` in
  modes with no UI (VR only — the panel exists in `--overlay` runs, where `ui on`
  disengages the overlay to reach it).
- **`screenshot FILE.png`** — dump the next fully-composited rendered frame (scene + UI,
  pre-swap glReadPixels) to a PNG. Works when the physical display is locked/headless —
  this is what makes remote visual validation possible without keyboard access.

That's the entire v0 verb set. No `trigger`, no `Moment`, no choreography primitive — an
agent or script that wants a "flash three images then fade to spiral" sequence composes it
client-side out of repeated `set`/`intensity`/`load pattern` calls timed by the client, the
same way a human operator would type them one at a time. Trance does not know what a sequence
of commands "means."

---

## 5. State (`status` verb)

`status` returns the single-line reply defined in §4 — deliberately minimal so
it's grep/parse-friendly from a shell script without a JSON library. If richer introspection
is needed later, that's a `get` key or a new verb, not a change to `status`'s shape.

---

## 6. Testing — Python is allowed *here only*

The channel is verb-in / reply-out over a loopback socket, so it is trivially testable from
any language, including plain shell (`echo "status" | nc host port`). A small **pytest**
client under `tests/` (or `netcat` in a shell script) connects, sends lines, and asserts the
reply lines. **This is the only place Python appears in the project, and it never ships in
the runtime or as a daemon.**

---

## 7. Build order (each step independently testable)

1. **`CommandChannel`** + a `--listen <port>` launch flag. Test with `netcat`: pipe a
   `status` line, get an `ok ...` reply. (No effects yet.)
2. **Drain + verb dispatch** in `main.cpp`'s loop; wire `start`/`stop`/`pause`/`resume`.
   Test each over the socket.
3. **`overlay on|off` / `overlay opacity`** — wire to the live overlay toggle (#27):
   apply/clear the click-through/translucency hints on the running window.
4. **`intensity VALUE`** — wire to whatever global scaling hook exists at implementation
   time (see §4 note on TBD wiring).
5. **`set KEY VALUE` / `get KEY`** — wire to the settings surface current at implementation
   time (protobuf `Program` fields today, JSON keys after that migration lands).
6. **`load pattern FILE` / `load session FILE`** — reuse the existing load paths.
7. **`status`** — single-line reply per §4/§5. <- **the v0 done-line: a controller drives
   playback and reads status end-to-end over the socket.**

---

## 8. MCP angle (out of scope here)

If an MCP client (Claude Desktop, or any other MCP host) should be able to drive trance, that
is a **separate, external, thin MCP server process** — not built in this repo, not built by
this spec. It maps MCP tool calls 1:1 onto the verbs in §4 (`start` tool -> `start` command,
`set_intensity(value)` tool -> `intensity VALUE` command, etc.) and speaks the line protocol
in §3 to trance over the loopback socket, exactly like the `netcat`/pytest test clients in
§6. An MCP-driven agent is not a special case inside trance — it is just another client of
these verbs, indistinguishable on the wire from a shell script.

---

## 9. Decisions / non-goals (from #21, amended 2026-07-01)

- **The daemon is a dumb settings-shaped effector, not a choreography engine.** No `Moment`
  proto, no `trigger()` primitive, no "conditioning moment" concept, no reading of browser or
  session context. Rescoped per owner decision 2026-07-01 — the prior draft's agent-driven
  choreography framing is off base for this spec.
- **Everything in the C++ binary. No separate bridge process. No Python in the runtime**
  (Python only as a test client, §6).
- Transport: **TCP loopback (`127.0.0.1`), no auth, no encryption** — loopback is the trust
  boundary; not a named pipe / stdio. (Unchanged from the prior rescope.)
- Protocol: **line-oriented plain text**, one command per line, one reply line per command —
  not JSON. Trivially scriptable with `echo`/`nc`.
- **Fixed v0 verb set** (§4): `start`/`stop`/`pause`/`resume`, `overlay on|off`/
  `overlay opacity`, `hide`/`show`, `intensity`, `set`/`get`, `load pattern`/`load session`,
  `status`. No verb is added speculatively.
- **`hide`/`show` is the silent-running primitive for MCP agents** (§4): an external
  controller that needs trance gone *now* (screen share starting, someone walks in) sends
  `hide` — one round-trip, no process kill, instant restore later with `show`.
- **Overlay verbs drive issue #27's click-through overlay live**: `overlay on|off` /
  `overlay opacity` apply/clear the hints on the running window at runtime (same seam the
  F2 UI's Overlay section uses).
- **Settings keys are TBD** pending the `.session`-to-JSON migration happening this sprint;
  this spec fixes the `set KEY VALUE` / `get KEY` verb shape, not the key names.
- **MCP integration is an external, separate process**, out of this repo's scope (§8).
