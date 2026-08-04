# What the visual engine actually does today (plain-English)

No jargon, no "lanes/voices/cadence." This is the ground truth the v3 intent grammar
must compile down to. If a proposed intent can't be expressed in the terms below, it
can't run — so this doc is the contract every later design depends on.

---

## 1. What the program is

Trance plays **fullscreen, rhythmic, flashing visuals** (images, text, spirals,
animations) timed to the frame, alongside a synthesized **entrainment audio bed**
(binaural/isochronic tones). The intent is psychovisual: rhythm, repetition, and
pairing aimed at a hypnosis-adjacent effect. Everything is deterministic integer
frame math — there is no real-time clock, no physics, no randomness except where the
pattern explicitly rolls a die.

## 2. The only things that can appear on screen

The engine has a **fixed, small vocabulary of draw operations** (`VisualControl` /
`VisualRender` in `api.h`). This is the floor. Nothing can be shown that isn't one of:

- **An image** — pulled from a **theme** (see §5). Can be drawn as a still, or as its
  **animated** form (a gif/webm). "Animation" is not a separate thing — it's just
  "draw this image slot as its moving version instead of a still."
  - Still-vs-animated is a **preference, not a partition, and never black**. A theme is
    whatever files its folder holds: a folder of nothing but gifs has zero stills, and a
    folder of nothing but jpegs has zero gifs. So `image` on an all-gif theme draws its
    gif, `anim` on a stills-only theme draws its still, and either falling short repeats
    the lane's last good frame. A draw op can only produce nothing when its lane has
    shown nothing at all yet. The pattern author asks for the *look*; which kind of file
    a given folder happens to contain is not something they can know.
  - And a substituted animation **still animates**. An `image` effect captures one `Image`
    into a register and the render block redraws that captured value for the rest of the
    cut — correct for a still, a freeze-frame for a gif. So when a register's lane holds
    an animation-only theme, the renderer re-reads the lane's live frame at draw time
    rather than the captured one (`VisualApiImpl::render_image`). Without it, one theme
    looked animated under a pattern drawing `anim` and like a stuck photograph under a
    pattern drawing plain `image`. Known edge: a fade whose `prev` register was captured
    before a theme swap shows the new lane's live frame for the rest of the fade, since
    the register records the slot and not which theme filled it.
  - An animation's **own frames run on its own clock**: each frame is shown for the
    duration its file specifies (a GIF's per-frame delay, a WebM's default duration /
    frame rate / block timestamps), independent of `global_fps` and of whatever the
    pattern driving it is doing. What the grammar controls is *which* animation is on
    screen and *when it is swapped* — not how fast it plays. (`AsyncStreamer::advance_frame`
    banks 1/global_fps of content time per tick and advances when the current frame's
    delay is paid off; `Streamer::frame_delay_seconds` carries the per-frame timing, which
    the ring buffer holds alongside each decoded frame. Before this, every animation
    played at a flat 15fps regardless of how it was authored.)
- **Text** — big foreground words, split by word / line / once.
- **Subtext** — a secondary scrolling text line.
- **Small text** — small caption text.
- **A spiral** — the rotating background; you can change its type/width and rotate it.
- **A font change**, and a **theme change** (swap which images/words are in play).

Each draw op takes a few numbers: **alpha** (opacity), **origin** and **zoom** (size /
position of the zoom), and for text a shadow origin/zoom. That's the entire painter's
palette. Any intent ("overload," "conditioning") must ultimately become some
combination of these ops with these numbers, over time.

## 3. How timing works: a tree of frame-counters

The schedule is a **tree**, and every node is just **a counter that counts frames**.
Advancing the whole tree one frame at a time *is* playback. The node types (`cyclers.h`),
in plain words:

| Node | What it does |
|---|---|
| **Action** (leaf) | The only node that *does* anything. Fires its effects on frame 0 of every N frames (N=1 = every frame). |
| **One-shot** | Run its children together, once. Lasts as long as its longest child. |
| **Parallel** | Run its children together, on repeat. |
| **Sequence** | Run its children one after another. |
| **Repeat** | Run one child N times. |
| **Offset** | Run one child, but phase-shifted by K frames. |
| **Burst** | A base loop randomly interrupted by a short burst, then a cooldown. (The one concession to "a little state.") |

So "slow flashes then fast flashes" is literally a Sequence of two sub-trees; "three
images at once" is a Parallel of three; "speed up" is a Sequence of Repeat blocks whose
counters shrink. **Lengths are exact integers** — a Sequence's length is the sum of its
children, a Parallel's is their least-common-multiple, etc. There is no call stack at
runtime; the tree's positions are the whole state.

## 4. What a leaf can do: effects

When an Action leaf fires, it runs an ordered list of **effects**. Two kinds:

- **Draw effects** — call one of the §2 ops (show an image, fire text, rotate the spiral…).
- **Scalar/register ops** — the *only* mutable memory the language has. There are no real
  variables. A tiny set exists purely to fake the few stateful built-ins: `set`, `inc`,
  `toggle`, a captured random `roll` (e.g. "pick 2, 4, or 8 once"), a `pulse` counter
  ("raise a flag every Nth fire"), `copy` (hand one image to another), and a single
  guard `when` ("do this only if register == N").

This register machinery is deliberately small. It exists because readable visual recipes need
bounded state such as "every third image" or "copy the last image before pulling the next one."

## 5. Themes, and the biggest limit

Images and words come from **themes**. At any moment the engine holds a small set of
"live" themes in slots, but the pattern language can only address **two**: **primary**
and **alternate** (plus "runtime" = whatever was last pulled, and "random"). That binary
is a hard limit baked into the data model. **Anything that wants 3+ themes at once — which
is exactly what associative conditioning across multiple concept-themes would need — is
impossible today** without a real change to the theme bank, the loader, and the draw API.
This is the single most important runtime limitation to know.

A theme's content is **strictly its own** (plus whatever it explicitly inherits, folded in
at load time — `docs/session-json-format.md` §3.3). ThemeBank checks that explicitly at the
point of selection rather than trusting `Shuffler`, which encodes membership as a priority
level and silently widens to the whole session's pool when a theme has nothing above the
base level — a theme with no gifs of its own would otherwise draw every other theme's
(`ThemeInfo::animation_members` / `image_members`; regression test: `theme_bank_test`
case 4).

Those two rules are a pair and must stay one: **content isolation says where a frame may
come from, the never-black fallback says what to draw when the preferred kind isn't
there.** Shipping the first without the second is what turned a cross-theme gif leak into
black screens — most themes in a real corpus are stills-only, so every `anim` draw landing
on one had been quietly borrowing a stranger's gif, and closing that off left nothing
behind it. `theme_bank_test` case 4 asserts both directions, and asserts the leak by the
fixture's image WIDTH rather than by emptiness precisely so that "it drew something" can
never again be mistaken for "it drew the right thing".

## 6. The render block: "what's drawn," separately from "when"

Recently the engine split into two halves:

- **The schedule** (§3–4) decides *when* things happen and writes registers.
- **The render block** decides *what is actually painted each frame*. It's a short list
  of draw statements (`image`, `text`, `subtext`, `small_text`, `spiral`), each with an
  optional condition and number expressions for alpha/zoom/origin. Those expressions are
  evaluated **every frame** against the live counters and registers (e.g. "zoom =
  0.4 × how-far-through-this-ramp-we-are"). Run by `render_eval.cpp`.

This is the part that's already "data, not code," and it's the natural lowering target
for v3's render shapes.

## 7. The contract for v3 (the compile-down invariant)

Whatever the intent grammar looks like, the compiler must turn each pattern into:

1. **A schedule tree** of the §3 node types, whose leaves fire
2. **effects** from the §4 vocabulary (draw ops + the register ops), feeding
3. **a render block** of §6 draw statements.

…all bottoming out in the §2 painter's palette and the §5 (currently binary) theme model.

That's the whole machine. v3 is a **friendlier front-end** that lowers to this — and
**must** lower to this (or to a deliberately-chosen extension of it, e.g. theme-index,
which is a real runtime project, not just parser work). An intent that can't be reduced
to "a counter tree firing these draw ops, painted by these statements" is, today,
unbuildable. Keep that test in hand for every idea: *what counter tree and what draw
statements would this become?*

---

*Source of truth: `src/trance/visual/api.h` (draw ops), `cyclers.h` (node types),
`pattern_ast.h` / `pattern_parser_v3.h` (effects + grammar), `render_eval.h` (render block),
`builtin_patterns_v3.cpp` (the 8 patterns as they exist). For the as-built developer
reference (with file/line detail) see `visuals.md`; this doc is the conceptual floor.*
