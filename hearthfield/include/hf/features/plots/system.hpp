// Plots: sow, grow, harvest. The feature that owns ready_tick.
//
// There is no tuning.hpp beside this, and the absence is the design: a farming
// game's feel IS its timers, and the timers are the crop catalogue in
// content/. The spec's §1 says so outright — "the timers are the level
// design" — so putting a grow time here would split the tuning in two.
#pragma once

#include "hf/content/crops.hpp"
#include "hf/runtime/events.hpp"
#include "hf/runtime/sim_system.hpp"

namespace hearthfield::plots {

class PlotsSystem final : public runtime::SimSystem {
 public:
  void Step(runtime::StepContext& ctx, const runtime::EventList& in,
            runtime::EventList& out) override;
};

}  // namespace hearthfield::plots
