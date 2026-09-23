// Placement: putting a building on the ground, and refusing to.
//
// A SLICE OF ITS OWN rather than a branch in economy, though it spends coin.
// Economy owns the purse and knows nothing about geometry; the ring knows
// geometry and nothing about money. The step that needs both is this one, and
// giving it to either neighbour would make that neighbour know the other's
// business (AGENTS.md's one-responsibility rule).
//
// THE RULE LIVES IN `runtime::CanPlace`, not here. The ghost the player drags
// asks the same function this does, so what they are shown and what the sim
// enforces cannot disagree — the failure mode being avoided is a green ghost
// over a cell the commit then refuses.
#pragma once

#include "hf/runtime/events.hpp"
#include "hf/runtime/sim_system.hpp"

namespace hearthfield::placement {

class PlacementSystem final : public runtime::SimSystem {
 public:
  void Step(runtime::StepContext& ctx, const runtime::EventList& in,
            runtime::EventList& out) override;
};

}  // namespace hearthfield::placement
