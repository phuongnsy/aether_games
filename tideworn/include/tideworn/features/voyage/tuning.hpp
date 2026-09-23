// Feature-local tuning (feel lives WITH its feature — AGENTS.md).
#pragma once

#include "aether/core/types.hpp"

namespace tideworn::voyage {

// Band thresholds in m/s, loosely Beaufort-shaped: the names must match what
// the sea LOOKS like at that wind, since both derive from the same number.
inline constexpr aether::F32 kFreshWind = 8.0f;
inline constexpr aether::F32 kGaleWind = 14.0f;
inline constexpr aether::F32 kStormWind = 20.0f;

}  // namespace tideworn::voyage
