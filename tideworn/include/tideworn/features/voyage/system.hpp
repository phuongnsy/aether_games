// The voyage's pure fixed step: (state, dt) -> state + events. Deterministic —
// no wall clock, no RNG — so a replayed run meets the same storm at the same
// second.
#pragma once

#include <vector>

#include "aether/core/types.hpp"
#include "tideworn/features/voyage/components.hpp"
#include "tideworn/features/voyage/events.hpp"

namespace tideworn::voyage {

// Wind at a point on the (looping) voyage clock — exposed for tests and for
// anything that wants to look AHEAD (a storm-warning HUD reads t + 60).
[[nodiscard]] aether::F32 WindAt(aether::F32 clock_s);

[[nodiscard]] Band BandFor(aether::F32 wind_speed);

void Step(VoyageState& state, aether::F32 dt, std::vector<BandChanged>& out);

}  // namespace tideworn::voyage
