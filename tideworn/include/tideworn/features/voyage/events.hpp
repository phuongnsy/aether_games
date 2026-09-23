// Events this feature emits (AGENTS.md: features talk via events).
#pragma once

#include "tideworn/features/voyage/components.hpp"

namespace tideworn::voyage {

// Fired once on each band transition — the hook for music, spray bursts,
// thunder scheduling and HUD warnings, so none of those poll the wind.
struct BandChanged {
  Band from = Band::kCalm;
  Band to = Band::kCalm;
};

}  // namespace tideworn::voyage
