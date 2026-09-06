#include "cr/view/hud.hpp"

#include <cmath>
#include <format>
#include <string>

#include "aether/core/math/math.hpp"  // Lerp
#include "aether/core/math/vec.hpp"
#include "aether/ui/context.hpp"
#include "cr/view/theme.hpp"

namespace game {

using namespace aether;

void DrawPlayHud(ui::Context& ui, const PlayHud& hud) {
  const Vec2 canvas = ui.Canvas();
  constexpr F32 kMargin = 18.0f;
  const F32 punch = hud.score_punch;
  // Coins top-left, time top-right (corners stay corners at any resolution).
  // The score brightens on a pickup, with a green "+1" rising + fading above.
  const Vec2 coins_at{kMargin, kMargin};
  const std::string coins =
      std::format("coins  {}/{}", hud.collected, hud.total);
  ui.Label(coins_at, coins,
           Lerp(kUiAccent, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, punch));
  if (punch > 0.0f) {
    const F32 rise = (1.0f - punch) * 22.0f;  // travels up as it fades
    ui.Label(
        Vec2{coins_at.x + ui.MeasureText(coins).x + 10.0f, coins_at.y - rise},
        "+1", Vec4{kUiGood.x, kUiGood.y, kUiGood.z, punch});
  }
  // Time: pulses red→white when the clock runs low (≤5s) to sell the tension.
  const std::string time = std::format("time  {:0.1f}", hud.time_left);
  Vec4 clock = kUiText;
  if (hud.time_left <= 5.0f) {
    const F32 beat = 0.5f + 0.5f * std::sin(hud.elapsed * 10.0f);
    clock = Lerp(kUiBad, Vec4{1.0f, 1.0f, 1.0f, 1.0f}, beat * 0.6f);
  }
  ui.Label(Vec2{canvas.x - kMargin - ui.MeasureText(time).x, kMargin}, time,
           clock);
  ui.LabelCentered(canvas.x * 0.5f, kMargin, hud.level_name, kUiTextDim);
}

}  // namespace game
