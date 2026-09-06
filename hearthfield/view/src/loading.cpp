#include "hf/view/loading.hpp"

#include <algorithm>

#include "hf/view/palette.hpp"

namespace hearthfield::view {
namespace {

using namespace aether;

// A DARK ground rather than the paper the rest of the UI sits on, and that is
// the one place this screen departs from the palette's usual roles. Paper is
// what a panel is made of; a full-bleed sheet of it reads as a blank document,
// and the first thing a player sees should read as the game dimming its lights.
constexpr Vec4 kGround = Rgb(0x1A150F);

// The bar's width as a fraction of the canvas, clamped so it is neither a
// hairline on a phone nor a horizon on a desktop.
constexpr F32 kBarFraction = 0.42f;
constexpr F32 kBarMin = 220.0f;
constexpr F32 kBarMax = 520.0f;
constexpr F32 kBarHeight = 10.0f;

}  // namespace

void DrawLoading(ui::Context& ui, F32 fraction, std::string_view label) {
  const Vec2 canvas = ui.Canvas();
  ui.Panel(ui::Rect{.x = 0.0f, .y = 0.0f, .w = canvas.x, .h = canvas.y},
           kGround);

  const F32 width =
      std::clamp(canvas.x * kBarFraction, kBarMin, std::min(kBarMax, canvas.x));
  const F32 x = (canvas.x - width) * 0.5f;
  const F32 y = canvas.y * 0.5f;

  // Set and restore: SetTextScale is the context's only lever for size, and a
  // scale left moved would resize the next frame's HUD.
  const F32 restore = ui.GetTextScale();
  ui.SetTextScale(kTitleScale);
  ui.LabelCentered(canvas.x * 0.5f, y - kGap - (kTitleScale * kGap),
                   "Hearthfield", kPaper);

  ui.Panel(ui::Rect{.x = x, .y = y, .w = width, .h = kBarHeight}, kBarEmpty);
  // AT LEAST A PIXEL once there is any progress at all: a bar that renders
  // zero-width for the first phase looks like a bar that never started.
  const F32 done = std::clamp(fraction, 0.0f, 1.0f);
  if (done > 0.0f) {
    ui.Panel(
        ui::Rect{
            .x = x, .y = y, .w = std::max(1.0f, width * done), .h = kBarHeight},
        kBarOk);
  }

  if (!label.empty()) {
    ui.SetTextScale(kBodyScale);
    ui.LabelCentered(canvas.x * 0.5f, y + kBarHeight + kPad, label, kDim);
  }
  ui.SetTextScale(restore);
}

}  // namespace hearthfield::view
