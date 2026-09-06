// The climb's HUD: how many lanterns are lit, how high you are, and whether a
// fall would cost you anything.
//
// Reads the snapshot only. It is deliberately three facts — a climb's whole
// state is "how far up, how much banked, how much at risk", and a HUD that
// says more than the player can act on is decoration.
#pragma once

#include "aether/resources/font.hpp"
#include "aether/ui/context.hpp"
#include "lantern/runtime/snapshot.hpp"

namespace lantern::view {

void DrawHud(aether::ui::Context& ui, const aether::resources::Font* font,
             const runtime::ViewSnapshot& snapshot, aether::Vec2 size);

}  // namespace lantern::view
