// Plain data only (games/CLAUDE.md feature template) — no logic here.
#pragma once

#include <vector>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace tideworn::school {

struct Fish {
  aether::Vec3 pos;  // world; y NEGATIVE below the surface
  aether::Vec3 vel;
};

// One school: every fish of one species, plus the wander clock the step's
// deterministic drift derives from (no RNG inside the step).
struct SchoolState {
  std::vector<Fish> fish;
  aether::U8 species = 0;  // index into content::kSpecies
  aether::F32 clock_s = 0.0f;
};

}  // namespace tideworn::school
