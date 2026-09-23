// One fixed step's player intent, latched once per render frame (the
// input-latch rule in AGENTS.md). The replay unit: a run is a sequence.
#pragma once

#include "aether/core/types.hpp"

namespace game {

struct LatchedInput {
  aether::F32 move = 0.0f;    // -1..1 horizontal steer
  bool jump_pressed = false;  // rising edge, latched this step
  bool jump_held = false;     // level state, for variable jump height
};

}  // namespace game
