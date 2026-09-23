// ViewSnapshot — the immutable frame the sim hands to `view`.
//
// Owned data only, no pointers back into sim state: the same rule the engine's
// RenderFrame follows, for the same reason (root AGENTS.md, seam 4).
#pragma once

#include <vector>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace lantern::runtime {

using aether::F32;
using aether::Usize;
using aether::Vec3;

struct LanternView {
  Vec3 position{0.0f, 0.0f, 0.0f};
  bool lit = false;
};

struct ViewSnapshot {
  Vec3 character{0.0f, 0.0f, 0.0f};
  Vec3 facing{0.0f, 0.0f, -1.0f};
  F32 speed = 0.0f;
  bool grounded = false;
  std::vector<LanternView> lanterns;
  Usize lit = 0;
  bool has_checkpoint = false;
  // Metres above the ground plane, which is what the HUD actually reports —
  // "how high have I got" is the question a climb answers.
  F32 height = 0.0f;
};

}  // namespace lantern::runtime
