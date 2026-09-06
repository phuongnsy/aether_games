// The round rules: ticks the timer, tallies coins (from this step's events, so
// runs after coins), unlocks the exit, wins at it. Emits ExitUnlocked/RoundWon.
#pragma once

#include "cr/runtime/sim_system.hpp"

namespace game {

class ProgressSystem final : public SimSystem {
 public:
  void Step(StepContext& ctx, const EventList& in, EventList& out) override;
};

}  // namespace game
