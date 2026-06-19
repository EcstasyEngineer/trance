#ifndef TRANCE_SRC_TRANCE_VISUAL_VISUAL_H
#define TRANCE_SRC_TRANCE_VISUAL_VISUAL_H
#include <common/media/image.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

class Cycler;
class VisualControl;
class VisualRender;

// Interface to an object which can render and control the visual state.
// These visuals are swapped out by the Director every so often for different
// styles.
class Visual
{
public:
  virtual ~Visual() = default;
  virtual void reset() {}
  Cycler* cycler();
  const Cycler* cycler() const;
  void render(VisualRender& api) const;

protected:
  void set_cycler(Cycler* cycler);
  void set_render(const std::function<void(VisualRender& api)>& function);

private:
  std::shared_ptr<Cycler> _cycler;
  std::function<void(VisualRender& api)> _render;
};

// The hardcoded visuals are retired -- each is now a compiled built-in pattern
// (builtin_patterns.cpp) with a named render preset (render_preset.cpp), proven
// render-equivalent before deletion: AccelerateVisual -> "accelerate", SlowFlashVisual
// -> "slow_flash", SubTextVisual -> "sub_text", FlashTextVisual -> "flash_text",
// SimpleVisual -> "simple" (enum PARALLEL), ParallelVisual -> "super_parallel",
// SuperFastVisual -> "super_fast". The Program::VisualType enum is unchanged, so
// existing .session files keep working.
//
// AnimationVisual remains the lone hardcoded class: it is the live reference the
// render-equivalence harness (pattern_render_test) advances alongside the compiled
// "animation" built-in. The engine itself plays the compiled version.
class AnimationVisual : public Visual
{
public:
  AnimationVisual(VisualControl& api);

private:
  Image _animation_backup;
  Image _current;
};

#endif