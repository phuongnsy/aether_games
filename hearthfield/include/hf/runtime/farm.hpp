// The farm's shared component data: the plot table every feature reads.
//
// It lives in runtime rather than in features/plots because the spec's §5
// graph points features AT runtime — production consumes what plots produced
// through shared data, never by calling plots — so a table two features share
// cannot live inside one of them. Same placement as coin_rush's
// cr/runtime/progress.hpp, and the opposite of tideworn, whose runtime depends
// on its features instead.
#pragma once

#include "aether/core/types.hpp"
#include "hf/content/crops.hpp"
#include "hf/runtime/tick.hpp"

namespace hearthfield::runtime {

using PlotId = aether::U32;
inline constexpr PlotId kNoPlot = ~PlotId{0};

enum class PlotState : aether::U8 {
  kEmpty,    // nothing sown; a tap sows
  kGrowing,  // sown, waiting on ready_tick
  kReady,    // ready_tick reached; a tap harvests
};

// Absolute ticks, never durations remaining. A remaining-time field would have
// to be decremented once per tick, which is exactly the per-step work the
// offline advance exists to avoid (spec §4.3).
struct Plot {
  PlotState state = PlotState::kEmpty;
  content::CropId crop = 0;
  Tick sown_tick = 0;
  Tick ready_tick = 0;
};

// 0 before sowing and after harvest, 1 when ready — what a view draws as a
// growth stage. Derived rather than stored, so it cannot disagree with the
// ticks that decide readiness.
[[nodiscard]] inline aether::F32 GrowthFraction(const Plot& plot, Tick now) {
  if (plot.state == PlotState::kReady) {
    return 1.0f;
  }
  // `now <= sown_tick` is guarded because these are UNSIGNED: a snapshot taken
  // on the sowing step itself would otherwise wrap to an enormous fraction.
  if (plot.state != PlotState::kGrowing || plot.ready_tick <= plot.sown_tick ||
      now <= plot.sown_tick) {
    return 0.0f;
  }
  const Tick span = plot.ready_tick - plot.sown_tick;
  const Tick done = now >= plot.ready_tick ? span : now - plot.sown_tick;
  return static_cast<aether::F32>(static_cast<aether::F64>(done) /
                                  static_cast<aether::F64>(span));
}

}  // namespace hearthfield::runtime
