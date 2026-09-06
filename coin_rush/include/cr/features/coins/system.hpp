// The coins system: each step it removes the coins the player overlaps + emits
// CoinCollected. Runs AFTER movement, reading its position via shared state.
#pragma once

#include "cr/runtime/sim_system.hpp"

namespace game {

class CoinsSystem final : public SimSystem {
 public:
  void Step(StepContext& ctx, const EventList& in, EventList& out) override;
};

}  // namespace game
