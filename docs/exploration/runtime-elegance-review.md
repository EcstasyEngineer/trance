# Runtime elegance review

Reviewed 2026-10-28 against `b50fbda68d2222e6558400e5f69a78b60a968ba3`. This is an analysis and bounded experiment, with no production implementation changes.

The strongest small simplification is to store image capture state as one record. The compiler also has two avoidable ownership/copying costs. A once-per-frame preparation boundary is useful longer-term, but should follow those smaller changes rather than grow into a new rendering framework.

## 1. Store a capture as one value

`src/trance/visual/pattern_runtime.h:14-23` spreads one image capture across three string-keyed maps: image, requested lane, and lane-generation stamp. Consequently, `compiled_visual.cpp:158-172` finds the other two records for every image, skips partially initialized entries, and `render_eval.cpp:23-36` does two separate lookups when drawing an image. Animation selection performs another lane lookup in `render_eval.h:290`.

A single `Capture { Image image; Slot requested_slot; uint32_t generation; }` map would express their shared lifetime directly. Reads would retrieve one capture; capture and refresh would update one record. Scalar selection state can remain separate. This is a bounded refactor, without a new abstraction hierarchy or a user-visible language feature.

Preserve the current readiness contract: an unsuccessful replacement retains the previous drawable image, records the requested lane, and leaves a stale generation so refresh retries. A retained image's actual historical provenance and its requested replacement lane are different concepts. Do not accidentally mark fallback media current merely because the capture is nonempty. An explicit readiness/provenance field would be clearer than inventing a second meaning for `generation - 1`, if this distinction is expanded later.

Acceptance: existing capture/refresh tests; a lane changes while unavailable, then becomes ready without another scheduled selection; independent simultaneous image outputs; identical later render passes. The readiness experiment below provides a baseline for one of these cases.

## 2. Keep compile-tree ownership in owning pointers

`src/trance/visual/pattern_compiler.cpp:27-36` accumulates recursively allocated children in `vector<Cycler*>`, then transfers them to `OccurrenceCycler`. The constructor in `cyclers.cpp:66-71` converts these to `unique_ptr` one at a time. If action construction or a later child construction throws before the parent owns the vector, previously constructed children have no owner.

Make the private recursive builder return `unique_ptr<Cycler>` and carry a vector of owning children through construction. Keep any existing raw-pointer public interface as a single release at its boundary if changing callers would add unnecessary churn. The node map stays a nonowning view of the finished tree. This reduces the ownership rules a reader must reconstruct and closes the exception path without a cleanup framework.

Acceptance: normal parser/runtime tests preserve node identity and clocks; an injected throwing action factory must unwind already-constructed children. No memory-leak measurement was performed here; the unowned interval is established by source inspection.

## 3. Avoid copying each descendant tree into every occurrence

`OccurrenceCycler` initializes `_spec(spec)` and immediately calls `_spec.children.clear()` in `src/trance/visual/cyclers.cpp:68-70`. `Node` owns descendant nodes by value, so the copy first recursively duplicates the entire subtree which is then discarded. Repeating this at every node copies each descendant once per ancestor. A deep chain therefore has quadratic node-copy work during construction. The scheduler also retains the effect list even though `CompiledVisual` has separately captured that list in the entry lambda.

The compact option is one AST copy at the compiler boundary, followed by moves down the private builder: detach/move children, capture entry effects, and move the childless scheduling node into its occurrence. This can be done alongside owning-pointer construction. Alternatively, use a small schedule value if the AST is being redesigned anyway; avoid manually duplicating the full AST merely to remove this copy.

Keep the cached parsed AST immutable, preserve action ordering, and do not move data that later render lowering still needs. No speedup is claimed: this is an identified copying cost, without a benchmark, and built-in trees are small.

## 4. A frame preparation boundary is a later candidate

Three independent render-time mutations currently need first-pass guards: stale capture refresh (`compiled_visual.cpp:136`), spiral phase advancement (`render_eval.cpp:61`), and animation resolution (`api.cpp:230`). Debug tallying has a fourth guard. Animation frames are recorded and replayed by draw-call order using a mutable vector and cursor (`api.cpp:201-239`). This is correct only while every pass follows the same call sequence; its overflow fallback pulls live media again.

A bounded direction is to prepare frame state once, then let each eye/desktop pass draw that state. Begin with refresh and scalar/spiral preparation; resolve media per output only when that permits deletion of the replay bookkeeping. Keep text layout and viewport-specific geometry per pass: `api.cpp` deliberately sizes text differently for VR, and `Director::render_image` also uses pass-dependent geometry. Playback time must remain the elapsed reference-frame amount, including zero while paused.

This is an optional architectural follow-up, not a recommendation for a broad rewrite now. Its acceptance is stricter than fewer lines: independent first/later-pass output equality, unchanged paused/90 Hz behavior, theme-change readiness, and no change to per-eye layout. No prototype of this refactor was implemented.

## Flash Text: current native behavior

The active built-in is `src/trance/visual/builtin_patterns_v4.cpp:259-352`. It already draws an opaque lower image plus a triangular-opacity upper image on 128-frame cadences, with the upper lane offset by 64 frames. Offset is also actively used by Parallel (32), Super Parallel (32 and 64), and Animation (48). Removing offset from the AST would alter four of the eight built-ins.

The bounded probe uses the real V4 parser, compiler, occurrence scheduler, capture effects, and render evaluator. It supplies valid synthetic CPU images and records the resulting draw calls; no production timing/rendering code is copied. A second render pass is checked for identical draws, pick count, and animation load count by the existing test helper. Frame numbers below are zero-based after one advance. Image numbers identify separate selections, not real media.

| Frame | Lower image / alpha | Upper image / alpha | Total selections |
| ---: | --- | --- | ---: |
| 0 | 11 / 1 | empty / 1 | 1 |
| 63 | 11 / 1 | empty / 0.015625 | 1 |
| 64 | 11 / 1 | 12 / 0 | 2 |
| 96 | 11 / 1 | 12 / 0.5 | 2 |
| 127 | 11 / 1 | 12 / 0.984375 | 2 |
| 128 | 13 / 1 | 12 / 1 | 3 |
| 191 | 13 / 1 | 12 / 0.015625 | 3 |
| 192 | 13 / 1 | 14 / 0 | 4 |
| 1023 | 25 / 1 | 26 / 0.984375 | 16 |
| 1024, root restart | 27 / 1 | 26 / 1 | 17 |

There is a valid alpha-1 lower capture on **1025 of 1025** sampled frames. The phase preroll suppresses upper-lane image selection until frame 64, despite advancing that lane's curve clock. It does not suppress the lower capture. At frame 128 the opaque upper image covers replacement of the lower image. The upper capture survives the root restart, making the next lower replacement covered too.

Deleting only the upper offset in the scratch AST keeps the opaque lower coverage, but both images are selected at frame 0 and again at frame 128. At frame 128 the new upper opacity is zero. Thus offset removal changes the handoff and selection cadence; it is not a harmless removal of unused syntax.

For synthetic media unavailable until frame 5, neither lane supplies a valid opaque image on frames 0-4. The lower capture refreshes successfully at frame 5 without a new scheduled selection. The probe therefore establishes that the current built-in does not **schedule** a leading/trailing black fade when its opaque media is ready. It does not establish a universal never-black guarantee for real media loading, transparent source images, or the complete presentation path.

Complementary opacity curves alone do not establish an opaque dissolve with ordinary source-over rendering. The renderer uses `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` in `director.cpp:483`; at two half-opacity layers the residual background weight is `(1 - 0.5) * (1 - 0.5) = 0.25`. Keep an opaque base and animate the overlay, or use an explicitly defined image-mix operation if a different result is desired. The current opaque-base recipe already avoids this particular dimming mechanism.

One endpoint qualification: `Director::update` calls `change_visual` immediately upon completion (`director.cpp:204-205`), before the next presentation. The probe directly advances and renders the compiled visual, so its frame-1023 sample verifies the scheduler/render endpoint state, not whether every player presentation shows that exact terminal frame. It also does not test decoded-media availability, the actual ThemeBank/VisualApi substitution path, GPU pixels, text/spiral compositing, or wall-clock playback.

## Reproduction and results

Local scratch sources are ignored under `build/runtime-elegance/`: `crossfade_probe.cpp` reuses the recording API/trace helper from `tests/v4_native_execution_test.cpp`, and `CMakeLists.txt` compiles the production sources into an isolated executable. It does not alter production tests or build configuration.

```powershell
$cmake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake -S build/runtime-elegance -B build/runtime-elegance/out -G 'Visual Studio 18 2026' -A x64
& $cmake --build build/runtime-elegance/out --config Release --target crossfade_probe --parallel 4
& build/runtime-elegance/out/Release/crossfade_probe.exe
```

Configuration, compilation, linking, and execution all passed. Reported outputs: `native opaque_capture_coverage=1025/1025`; `no_offset_experiment opaque_capture_coverage=1025/1025`; readiness assertion passed with exactly five frames lacking the lower opaque capture. This local scratch harness is not part of the public tracked source; the production test helper and source locations above preserve the basis of the experiment.
