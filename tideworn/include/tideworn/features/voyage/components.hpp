// Plain data only (AGENTS.md feature template) — no logic here.
#pragma once

#include "aether/core/types.hpp"

namespace tideworn::voyage {

// Coarse weather bands, thresholded on wind. What gameplay and presentation
// key off (music, spray, HUD) — the CONTINUOUS wind is what the sea reads.
enum class Band : aether::U8 { kCalm, kFresh, kGale, kStorm };

struct VoyageState {
  aether::F32 clock_s = 0.0f;      // voyage time, fixed-step accumulated
  aether::F32 wind_speed = 5.0f;   // m/s, interpolated from the schedule
  aether::F32 time_of_day = 0.0f;  // 0..1 over content::kDayLengthS
  Band band = Band::kCalm;
};

}  // namespace tideworn::voyage
