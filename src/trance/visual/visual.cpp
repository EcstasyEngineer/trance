#include <trance/visual/visual.h>
#include <trance/visual/cyclers.h>

void Visual::set_cycler(Cycler* cycler)
{
  _cycler.reset(cycler);
}

void Visual::set_render(const std::function<void(VisualRender& api)>& function)
{
  _render = function;
}

Cycler* Visual::cycler()
{
  return _cycler.get();
}

const Cycler* Visual::cycler() const
{
  return _cycler.get();
}

void Visual::render(VisualRender& api) const
{
  _render(api);
}
