# Spec — Ambient daemon mode + MCP bridge (issue #21)

Implementation spec refining GitHub issue #21. Trance becomes a **dumb effector**: a
persistent run mode whose visuals are driven on demand by an external agent over a local
command channel. Trance senses nothing; it exposes "do X to the screen now" verbs. All
context-reading and decision-making live in the agent/bridge layer (the easy language).

This spec is the buildable plan for **v0**; v1/v2 are sketched at the end.

---

## 1. Architecture (the spine, build in order)

```
  agent  ⇄  MCP server (Python)  ⇄  localhost TCP socket  ⇄  trance (C++)
                                                              │
                                          reader thread → mutex queue → render loop drains 1×/frame
```

Four layers, each hangs off the prior:
1. **In-process command channel** (C++) — the keystone.
2. **Line-delimited JSON protocol** — verbs + params only, with per-line ack/nack.
3. **MCP bridge** (separate Python process) — no MCP code in C++.
4. **State as an MCP resource** — reuse `ThemeBank::debug_snapshot()` serialized to JSON.

---

## 2. The command channel (C++) — `src/trance/net/command_channel.{h,cpp}`

The **inverse of the ThemeBank async-loader pattern**: there a worker loads while the render
thread reads atomics; here a worker *receives* while the render thread *drains a queue*. Same
threading discipline → idiomatic.

```cpp
class CommandChannel {
public:
  // Binds 127.0.0.1:<port>, spawns the reader thread. token is checked at the reader boundary.
  CommandChannel(uint16_t port, std::string token);
  ~CommandChannel();                       // signals stop, joins the reader thread

  struct Command { std::string verb; std::string json; };  // raw line, parsed to verb+payload
  // Render-thread only: move out everything received since the last call.
  std::vector<Command> drain();
  // Render-thread only: reply on the same connection (ack/nack per command).
  void reply(uint64_t conn_id, const std::string& line);

private:
  void reader_loop();                      // owns the socket; pushes into _queue under _mutex
  std::mutex _mutex;
  std::vector<Command> _queue;             // guarded by _mutex
  std::atomic<bool> _running;
  std::thread _reader;
  // winsock on Windows (ws2_32, already linked), BSD sockets on Linux.
};
```

**Threading invariant (load-bearing):** the reader thread only ever pushes raw lines into the
mutex-guarded queue. **`Director`/`ThemeBank` are mutated only at the drain point on the render
thread** — never from the reader thread. This is exactly the ThemeBank discipline (worker side
touches only its own atomics; the render thread owns the mutations).

**Transport:** TCP localhost + a shared token (checked at the reader boundary on connect).
Cross-platform, trivial from Python, testable with `netcat`. Token from a launch flag/env.

---

## 3. Protocol

Line-delimited JSON, **one object per line**, one ack/nack line back per command. Trance parses
*verbs and params only* — never "intent." No schema negotiation, no streaming.

```
→ {"verb":"spiral","on":true,"speed":3.0}
← {"ok":true}
→ {"verb":"trigger","moment":{...}}
← {"ok":false,"error":"Moment.track[0]: unknown effect"}
```

Parse + dispatch happens in the drain loop (`main.cpp`'s per-frame loop, right after
`handle_events`). Unknown verb / bad JSON → `{"ok":false,"error":...}`, never a crash.

---

## 4. Verb surface (implement low→high invasiveness)

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
the wire** (protobuf's `JsonStringToMessage`). The agent emits JSON; trance parses → validates →
compiles into a **transient cycler tree** and runs it, decaying to idle when complete.

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
`Moment → Parallel(Offset(OneShot(effect-leaf)), …)`. This **reuses the existing cycler
machinery** (`cyclers.{h,cpp}` — Parallel/Offset/OneShot already exist) and the v2 effect-leaf
vocabulary; the only new code is (a) the Moment→cycler builder and (b) the effect leaves the
tracks reference (spiral/text/intensity/fade). ms→frames uses the program's `global_fps`. The
built tree is handed to `Director` as a transient `Visual` (same path as `CompiledVisual`).

---

## 6. State as an MCP resource (v1)

Expose current visual / theme / running-state / idle-vs-active as a **readable resource** so the
agent can see the screen and react. **Reuse `ThemeBank::debug_snapshot()`** — the F1 overlay
already produces a structured live-state snapshot; serialize it to JSON over the same channel
(`status` verb returns it). The overlay work doubles as the agent's observability layer; no new
read-model needed.

---

## 7. The MCP bridge (Python) — `bridge/trance_mcp/`

A separate process using the mature MCP SDK. Chain: `agent ⇄ MCP server ⇄ socket ⇄ trance`.
**No MCP code in C++.** Auth/token, reconnect, and the evolving tool surface live here, in the
easy language. Each MCP tool maps 1:1 to a verb line; `trigger` takes a `Moment` JSON the agent
composes. Ships as a small `pip`-installable package + a `claude_desktop_config`/MCP manifest
snippet.

---

## 8. v0 build order (each step independently testable)

1. **`CommandChannel`** + a `--listen <port> --token <t>` launch flag. Test with `netcat`:
   pipe a `{"verb":"status"}` line, get a JSON ack. (No effects yet.)
2. **Drain + verb dispatch** in `main.cpp`'s loop; wire `pause`/`resume`/`set_speed`/`stop`/
   `status`/`load_session`. Test each over `netcat`.
3. **`Moment` proto + interpreter** + `trigger`. Test: send a composed spiral+text moment JSON,
   watch it run and decay. ← **the v0 done-line: agent throws a moment end-to-end.**
4. **Python MCP bridge** exposing the verbs + `trigger` as MCP tools.

## 9. v1 / v2 (post-v0)

- **v1:** the `status` state-resource (reuse `debug_snapshot`) + `set_speed`/`force_visual`/
  `pin_theme`/`add_media`/`play_audio`. → agent sees the screen and reacts.
- **[ blocked on #20 SFML 3 ]** overlay window mode (W3): borderless, always-on-top,
  non-focus-stealing/transparent — touches the same `render.cpp` windowing / `sf::Style` code
  SFML 3 rewrites, so do it once against the final API.
- **v2:** `--ambient` idle/interrupt run mode (W4) — blank/quiet by default, animating only when
  a `trigger`/`spiral` fires, then decaying. New launch flag. This is what makes it a daemon,
  not a remote control.

## 10. Decisions / non-goals (from #21)

- Transport: **TCP localhost + shared token** (not a named pipe / stdio) — cross-platform, easy
  from Python, testable with netcat.
- `trigger` moment format: **data-defined from day one** (a typed proto accepted as JSON).
- **No MCP code in C++**; **no schema negotiation**; **no streaming**; trance **senses nothing**.
- Overlay window + `--ambient` are **gated behind SFML 3 (#20)** for the windowing rewrite.
