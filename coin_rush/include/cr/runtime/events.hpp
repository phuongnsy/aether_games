// The frame-buffered event bus payloads: sim→view (and inter-feature) events
// drained by view after the fixed step. Add variants as features grow.
#pragma once

#include <variant>
#include <vector>

#include "aether/core/math/vec.hpp"

namespace game {

struct Jumped {};  // launched off the ground
struct Landed {
  aether::Vec2 point;
};  // feet touched down → landing puff
struct WallHit {
  aether::Vec2 point;
};  // ran into a wall → sparks
struct CoinCollected {
  aether::Vec2 point;
};  // → pop + sound + shake + score juice
struct ExitUnlocked {};  // all coins collected → the exit brightens
struct RoundWon {
  int stars;
};  // reached the exit → win flash + result

using GameEvent = std::variant<Jumped, Landed, WallHit, CoinCollected,
                               ExitUnlocked, RoundWon>;
using EventList = std::vector<GameEvent>;

}  // namespace game
