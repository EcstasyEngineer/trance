# Spec — Agent-controllable trance (in-process command channel, issue #21)

Implementation spec refining GitHub issue #21. Trance becomes a **dumb effector**: a run
mode whose visuals are driven on demand by an external controller over a **local socket**.
Trance senses nothing; it exposes "do X to the screen now" verbs. The decision-making lives
in whatever connects (a script, a test, or later an agent).

**Scope decisions (this rewrite):** the whole feature lives **in the C++ app**. There is
**no separate bridge process** and **no Python** in the runtime — Python is permitted *only*
as a test client. The transport is **loopback TCP with no auth and no encryption** (it binds
`127.0.0.1` only; if you can open a socket to localhost you are already on the machine). No
token, no TLS, no schema negotiation, no streaming.

This spec is the buildable plan for **v0**; v1/v2 are sketched at the end.

---

## 1. Architecture (one process)

```
  controller (script / test / agent)  --localhost TCP, line-JSON-->  trance (C++)
                                                                       |
                                       reader thread -> mutex queue -> render loop drains 1x/frame
```

Three things, each hangs off the prior, **all in the trance binary**:
1. **In-process command channel** (C++) — the keystone: a loopback socket + reader thread.
2. **Line-delimited JSON protocol** — verbs + params only, with per-line ack/nack.
3. **The `trigger(moment)` primitive** — a self-contained, data-defined timed sequence that
   compiles to a transient cycler tree and runs, decaying to idle.

No MCP SDK, no bridge, no auth layer. If MCP-client (e.g. Claude Desktop) integration is ever
wanted, it is a **thin in-process adapter added to the C++ app later** (§9) — never a
separate Python process.

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

  struct Command { uint64_t conn_id; std::string verb; std::string json; };
  // Render-thread only: move out everything received since the last call.
  std::vector<Command> drain();
  // Render-thread only: reply on the same connection (ack/nack per command).
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
mutex-guarded queue. **`Director`/`ThemeBank` are mutated only at the drain point on the
render thread** — never from the reader thread. This is exactly the ThemeBank discipline
(worker side touches only its own atomics; the render thread owns the mutations).

**Transport:** TCP on `127.0.0.1` only. No token, no TLS — loopback is the trust boundary.
Cross-platform, trivial to drive from a test, inspectable with `netcat`. Port comes from a
launch flag (`--listen <port>`), default off (no socket unless asked).

---

## 3. Protocol

Line-delimited JSON, **one object per line**, one ack/nack line back per command. Trance
parses *verbs and params only* — never "intent."

```
-> {"verb":"spiral","on":true,"speed":3.0}
<- {"ok":true}
-> {"verb":"trigger","moment":{...}}
<- {"ok":false,"error":"Moment.track[0]: unknown effect"}
```

Parse + dispatch happens in the drain loop (`main.cpp`'s per-frame loop, right after
`handle_events`). Unknown verb / bad JSON -> `{"ok":false,"error":...}`, never a crash.

---

## 4. Verb surface (implement low->high invasiveness)

- **Transport:** `pause`, `resume`, `set_speed`, `stop`
- **Lifecycle:** `load_session(path)`, `status`
- **Live overrides:** `spiral(on,speed)`, `set_intensity(0..1)`, `force_visual(type)`, `pin_theme(name)`
- **Content:** `flash_text(lines,style)`, `add_media(paths)`, `play_audio(channel,path)`
- **The money primitive:** `trigger(moment_json)` — a self-contained, data-defined timed
  sequence; runs, then decays to idle.

v0 ships: `pause`/`resume`/`stop`/`set_speed`/`load_session`/`status`/`spiral`/`trigger`.

---

## 5. The `Moment` primitive (the core of v0)

The codebase is protobuf-native, so define `Moment` as a **proto message accepted as JSON over
the wire** (protobuf's `JsonStringToMessage`). The controller emits JSON; trance parses ->
validates -> compiles into a **transient cycler tree** and runs it, decaying to idle when
complete.

Add to `trance.proto`:
```proto
message Moment {
  string name = 1;
  uint32 duration_ms = 2;
  repeated Track tracks = 3;            // run in Parallel
}
message Track {
  uint32 start_ms = 1;                  // -> Offset cycler (frames = ms * fps / 1000)
  uint32 duration_ms = 2;              // -> OneShot cycler
  oneof effect {
    Spiral spiral = 3;                  // speed, direction
    TextBurst text = 4;                 // lines, style, font
    IntensityRamp intensity = 5;        // keyframed 0..1 -> flash rate / opacity / draw-depth
    Fade fade = 6;
  }
}
```

**Interpreter** (`src/trance/net/moment_interpreter.{h,cpp}`):
`Moment -> Parallel(Offset(OneShot(effect-leaf)), ...)`. This **reuses the existing cycler
machinery** (`cyclers.{h,cpp}` — Parallel/Offset/OneShot already exist) and the v2 effect-leaf
vocabulary; the only new code is (a) the Moment->cycler builder and (b) the effect leaves the
tracks reference (spiral/text/intensity/fade). ms->frames uses the program's `global_fps`. The
built tree is handed to `Director` as a transient `Visual` (same path as `CompiledVisual`).

---

## 6. State (`status` verb)

Expose current visual / theme / running-state / idle-vs-active as a JSON blob so the
controller can see the screen and react. **Reuse `ThemeBank::debug_snapshot()`** — the F1
overlay already produces a structured live-state snapshot; serialize it to JSON and return it
from the `status` verb. The overlay work doubles as the controller's observability layer; no
new read-model needed.

---

## 7. Testing — Python is allowed *here only*

The channel is verb-in / ack-out over a loopback socket, so it is trivially testable from any
language. A small **pytest** client under `tests/` (or `netcat` in a shell) connects, sends a
line, and asserts the ack — including composing a `Moment` JSON and checking it runs. **This is
the only place Python appears in the project, and it never ships in the runtime or as a daemon.**

---

## 8. v0 build order (each step independently testable)

1. **`CommandChannel`** + a `--listen <port>` launch flag. Test with `netcat`: pipe a
   `{"verb":"status"}` line, get a JSON ack. (No effects yet.)
2. **Drain + verb dispatch** in `main.cpp`'s loop; wire `pause`/`resume`/`set_speed`/`stop`/
   `status`/`load_session`. Test each over the socket.
3. **`Moment` proto + interpreter** + `trigger`. Test: send a composed spiral+text moment JSON,
   watch it run and decay. <- **the v0 done-line: a controller throws a moment end-to-end.**

## 9. v1 / v2 (post-v0)

- **v1:** the richer `status` snapshot + `set_speed`/`force_visual`/`pin_theme`/`add_media`/
  `play_audio`. -> a controller sees the screen and reacts.
- **[ blocked on #20 SFML 3 ]** overlay window mode (W3): borderless, always-on-top,
  non-focus-stealing/transparent — touches the same `render.cpp` windowing / `sf::Style` code
  SFML 3 rewrites, so do it once against the final API.
- **v2:** `--ambient` idle/interrupt run mode (W4) — blank/quiet by default, animating only when
  a `trigger`/`spiral` fires, then decaying. New launch flag. This is what makes it a daemon,
  not a remote control.
- **Optional MCP-client front:** if an MCP client (Claude Desktop, etc.) should call trance as a
  tool, add a **thin in-process JSON-RPC tools adapter to the C++ app** that maps `tools/call`
  to the existing verbs. Per the scope decision this is **not** a separate Python bridge. Defer
  until there is a concrete reason — the socket verbs already cover script/test/agent control.

## 10. Decisions / non-goals (from #21, amended)

- **Everything in the C++ binary. No separate bridge process. No Python in the runtime**
  (Python only as a test client, §7).
- Transport: **TCP loopback (`127.0.0.1`), no auth, no encryption** — loopback is the trust
  boundary; not a named pipe / stdio.
- `trigger` moment format: **data-defined from day one** (a typed proto accepted as JSON).
- **No schema negotiation; no streaming; trance senses nothing.**
- Overlay window + `--ambient` are **gated behind SFML 3 (#20)** for the windowing rewrite.
