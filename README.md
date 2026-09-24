# trance

trance is a native visual and audio player for self-hypnosis sessions. It combines
images, GIF/WebM animations, text, spirals, and optional synthesized audio. Windows
is the primary platform; Linux/WSL is experimental.

## Start playing

1. Download and extract a [release](../../releases/latest).
2. Run `trance.exe path/to/my.session.json`, or launch with no session argument
   to create `default.json` from media in the working directory.
3. Press **F2** to edit, **M** to mute, or **Escape** to quit.
   **Shift+F11** hides, pauses, and mutes; press again to restore.

F2 saves committed edits to the loaded session automatically. **System → Export**
writes a separate copy. Back up hand-authored files before editing them in F2:
`_` comment keys are accepted on load but are not preserved on save.

## Sessions and patterns

A session is a JSON file containing themes (media pools), programs (theme/visual
weights, display settings, and an audio bed), and a playlist (timed program
changes, cues, branches, and subroutines). Media paths resolve relative to the
session file. Scan themes include added files on the next load. Legacy upstream
`.session` files migrate to JSON without replacing the original.

All eight built-ins use the v3 pattern language available to custom patterns.
It composes timed effects, nested schedules, curves, and burst interruptions.
See the [authoring guide](docs/authoring-v3-patterns.md).

```powershell
# Play, force a built-in, or force a custom source.
.\trance.exe .\my.session.json
.\trance.exe --visual=super_fast .\my.session.json
.\trance.exe --pattern=.\my.pattern .\my.session.json

# Validate built-ins and session custom patterns without a window.
.\trance.exe --lint .\my.session.json

# Bundle session and referenced assets as a standard ZIP.
.\trance.exe --export_archive=.\portable.trance .\my.session.json
```

`--visual` accepts `accelerate`, `slow_flash`, `sub_text`, `flash_text`,
`simple`, `super_parallel`, `animation`, and `super_fast`. It cannot be combined
with `--pattern`. In session JSON, `simple` uses enum name `parallel`.

## Desktop control and automation

`--overlay` makes the window translucent, always on top, and click-through.
Use Shift+F11, the Windows tray icon, or external controls to leave it.
See [controls](docs/controls.md).

`--command_port=9191` opens a loopback line-protocol socket.
`--mcp --hidden` lets an MCP host launch the binary as a stdio server, initially
hidden, paused, and muted. Add `--muted` to remain muted after showing.
See [MCP setup](docs/mcp-install.md) and the
[command reference](docs/spec-mcp-ambient-daemon.md).

## VR setup

OpenXR is an optional Windows output of the desktop renderer. The application
probes for the active runtime and an available headset, attaches when possible,
and keeps the desktop session running when XR detaches. There is no renderer
selector. F1/F2 remain on the desktop.

**Physical-headset validation is still pending.** See
[OpenXR output](docs/spec-xr-unified.md) for implementation and hardware acceptance
checks. Attachment, pacing, colors, and recovery are not hardware-verified.

## Documentation

| Task | Reference |
|---|---|
| Keys, tray, hide, overlay | [Controls](docs/controls.md) |
| Media and playlist flow | [Sessions and playlists](docs/sessions-and-playlists.md) |
| JSON fields | [Session JSON](docs/session-json-format.md) |
| Built-ins and selection | [Visuals](docs/visuals.md) |
| Music, theme audio, synthesis | [Audio](docs/audio.md) |
| Write patterns | [Authoring](docs/authoring-v3-patterns.md), [grammar](docs/spec-grammar-v3.md) |
| Planned authoring improvements | [Roadmap](docs/authoring-roadmap.md), [tracking issue #66](https://github.com/EcstasyEngineer/trance/issues/66) |
| Implementation | [Architecture](docs/architecture.md), [engine](docs/engine-today.md) |
| Guarantees and gaps | [Architecture maturity](docs/architecture-maturity.md) |

## Build and test

Requires C++17, CMake 3.25+, and a bootstrapped
[vcpkg](https://github.com/microsoft/vcpkg) checkout. Set `VCPKG_ROOT` to that
checkout. Dependencies are restored through `vcpkg.json`; use the checked-in
CMake presets. Windows uses the Visual Studio 2026 C++ toolchain.

```powershell
cmake --preset windows-msvc -DTRANCE_BUILD_TESTS=ON
cmake --build --preset windows-release
ctest --test-dir build/windows-msvc -C Release --output-on-failure
.\build\windows-msvc\Release\trance.exe .\my.session.json
cmake --install build/windows-msvc --config Release --prefix dist
```

The existing `build.bat` locates Visual Studio's tools and vcpkg, then invokes
these presets. Windows dependencies use `x64-windows-static-md`; the install
target packages `trance.exe`.

Linux uses Ninja and `linux-gcc` / `linux-release`. Debug uses
`linux-gcc-debug` / `linux-debug`; Windows uses `windows-debug`.
Linux also needs development packages for X11/Xext, OpenGL, and the vcpkg ports.
Outputs are under `build/<configure-preset>/`.

Build all test targets before CTest. `grammar_lint` and
`phase_execution_test` cover pattern compilation and execution;
`session_json_test` and `playlist_runner_test` cover session behavior.
`theme_bank_test` requires an OpenGL context and is labeled `gpu`
(`ctest -LE gpu` excludes it). The live command-channel harness is separate:
`tests/qa_command_channel.py`.

## License

[WTFPL](LICENSE).
