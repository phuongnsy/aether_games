#include "lantern/view/hud.hpp"

#include <format>
#include <string>

namespace lantern::view {

using aether::Vec2;
using aether::Vec4;

void DrawHud(aether::ui::Context& ui, const aether::resources::Font* font,
             const runtime::ViewSnapshot& snapshot, Vec2 size) {
  constexpr Vec4 kWarm{1.0f, 0.88f, 0.66f, 1.0f};
  constexpr Vec4 kDim{0.62f, 0.64f, 0.70f, 1.0f};
  constexpr Vec4 kRisk{0.95f, 0.62f, 0.42f, 1.0f};

  ui.Label(
      Vec2{24.0f, 20.0f},
      std::format("lanterns  {} / {}", snapshot.lit, snapshot.lanterns.size()),
      kWarm, font);
  ui.Label(Vec2{24.0f, 48.0f}, std::format("height  {:.1f} m", snapshot.height),
           kDim, font);

  // What a fall would cost, which is the only number that changes a decision.
  // Before the first lantern there IS no checkpoint, and saying so is the
  // difference between a cautious player and a surprised one.
  ui.Label(Vec2{24.0f, size.y - 44.0f},
           snapshot.has_checkpoint ? "a fall returns you to your last lantern"
                                   : "no lantern lit — a fall costs everything",
           snapshot.has_checkpoint ? kDim : kRisk, font);
}

}  // namespace lantern::view
