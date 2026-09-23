// School feel — feature-local constants (AGENTS.md: feel lives WITH its
// feature). Weights are accelerations in m/s^2 at unit error.
#pragma once

#include "aether/core/types.hpp"

namespace tideworn::school {

inline constexpr aether::F32 kSeparationRadius = 0.6f;  // x species scale
inline constexpr aether::F32 kSeparation = 6.0f;
inline constexpr aether::F32 kCohesion = 0.55f;
inline constexpr aether::F32 kAlignment = 1.2f;
// The depth band is a hard habitat, so its spring is the strongest pull.
inline constexpr aether::F32 kDepthSpring = 2.2f;
inline constexpr aether::F32 kHomeSpring = 0.35f;
// Catch-up: the speed ceiling rises past the home radius so a school can
// re-join a boat under way, then settles back to cruise inside it.
inline constexpr aether::F32 kCatchUpPerMetre = 0.5f;
inline constexpr aether::F32 kCatchUpMax = 3.0f;
// Deterministic wander: per-fish sinusoids on the school clock.
inline constexpr aether::F32 kWander = 0.5f;
inline constexpr aether::F32 kFleeRadius = 3.5f;
inline constexpr aether::F32 kFlee = 8.0f;
inline constexpr aether::F32 kDrag = 0.8f;  // 1/s, caps speed against drive

}  // namespace tideworn::school
