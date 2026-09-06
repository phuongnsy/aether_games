// The school's pure fixed step: classic boids (separation / alignment /
// cohesion — GEA names flocking as gameplay-layer group behaviour and leaves
// the algorithm to Reynolds) plus a depth-band spring and a boat-anchored
// home. Deterministic: wander is a per-fish sinusoid, RNG only at Spawn.
#pragma once

#include <array>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "tideworn/features/school/components.hpp"

namespace tideworn::school {

// What the step reads from the wider world — a narrow query seam, so tests
// hand in trivial values.
struct Env {
  aether::Vec2 anchor_xz;  // where the school homes (near the boat)
  // What it flees when close: the hull, and the diver when one is in the
  // water. Fixed-size on purpose — the sim has exactly these two threats.
  std::array<aether::Vec2, 2> threats_xz{};
  aether::U32 threat_count = 0;
};

// Deterministic spawn into the species' band around `anchor`. Positions come
// from `seed` alone — the world RNG's draw order stays untouched.
void Spawn(SchoolState& state, aether::U8 species, aether::Vec2 anchor,
           aether::U32 seed);

void Step(SchoolState& state, const Env& env, aether::F32 dt);

}  // namespace tideworn::school
