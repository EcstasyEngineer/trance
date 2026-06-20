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

#endif