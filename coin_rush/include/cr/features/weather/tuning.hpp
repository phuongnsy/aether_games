// Weather feel constants (feature-local). Rain wets platforms (→ slippery);
// snow piles into standable ledges.
#pragma once

#include "aether/core/types.hpp"

namespace game {

constexpr aether::F32 kRainWindResp = 9.0f;    // rain's susceptibility to wind
constexpr aether::F32 kWetPerHit = 0.06f;      // wetness added per rain impact
constexpr aether::F32 kWetDryRate = 0.28f;     // wetness decay/sec
constexpr aether::U64 kSnowSurface = 7000;     // accumulation id (ground snow)
constexpr aether::F32 kSnowRate = 520.0f;      // snow flakes/sec while snowing
constexpr aether::F32 kSnowThickness = 40.0f;  // max snow depth (world units)
constexpr aether::F32 kSnowFillPerHit = 1.6f;  // snow depth added per impact
constexpr aether::F32 kSnowWindResp = 2.0f;    // snow drifts more gently

}  // namespace game
