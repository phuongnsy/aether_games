// Round progression state — sim-owned (updated by the progression system),
// read by the HUD + result screen. Shared world state, lives in WorldView.
#pragma once

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace game {

struct RoundProgress {
  int total = 0;                      // coins in the level (set as they build)
  int collected = 0;                  // coins picked up this round
  aether::F32 time_left = 0.0f;       // soft timer → star rating
  int stars = 0;                      // 1-3 on win
  bool round_over = false;            // reached the exit
  bool exit_unlocked = false;         // all coins collected
  aether::Vec2 exit_pos{0.0f, 0.0f};  // goal position
  aether::F32 win_radius = 0.0f;      // reach distance to the exit
};

}  // namespace game
