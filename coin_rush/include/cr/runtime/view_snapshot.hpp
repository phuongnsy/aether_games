// The sim→view data contract: GameWorld produces a ViewSnapshot each step, and
// view/ consumes ONLY this (+ the drained EventList), never live sim state.
#pragma once

#include <string_view>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace game {

// The in-play HUD data (coins/timer/level + the pickup-juice value).
struct PlayHud {
  int collected = 0;
  int total = 0;
  aether::F32 time_left = 0.0f;
  aether::F32 elapsed = 0.0f;      // drives the low-time pulse
  aether::F32 score_punch = 0.0f;  // 1→0 pickup flash
  std::string_view level_name;
};

// Immutable per-frame view of the sim: derived state view needs that isn't
// already reachable in the scene it renders.
struct ViewSnapshot {
  PlayHud hud;
  aether::Vec2 player_pos{0.0f,
                          0.0f};  // for the coin spotlight + camera framing
  bool round_over = false;
};

}  // namespace game
