// Gameplay constants the composition root wires in. Unit-specific tuning
// (controller feel, weather rates, lighting) lives WITH its own unit.
//
// APP-PRIVATE, and here rather than at the game root because
// `app/coin_rush.cpp` is its only consumer — it sat beside the layer
// directories with the game root on the include path to reach it, which is the
// one place in any game that was neither a public `<layer>/include/` nor a
// private `<layer>/src/`. Its old comment claimed several units shared it; none
// did.
#pragma once

#include "aether/core/types.hpp"

namespace game {

constexpr aether::F32 kTile = 48.0f;       // world units per level cell
constexpr aether::U32 kLayerWall = 0x1;    // solid; player + weather resolve
constexpr aether::U32 kLayerCoin = 0x2;    // trigger; overlap = pickup
constexpr aether::F32 kTimeLimit = 30.0f;  // par time (soft; drives the stars)

}  // namespace game
