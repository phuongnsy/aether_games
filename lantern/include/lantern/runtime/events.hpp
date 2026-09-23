// The frame's events: how the sim tells `view` what happened without knowing
// view exists (AGENTS.md, the event bus).
#pragma once

#include <variant>
#include <vector>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace lantern::runtime {

using aether::F32;
using aether::Usize;
using aether::Vec3;

struct LanternLit {
  Usize index = 0;
  Vec3 position{0.0f, 0.0f, 0.0f};
};

struct Fell {
  Vec3 from{0.0f, 0.0f, 0.0f};
};

struct Respawned {
  Vec3 at{0.0f, 0.0f, 0.0f};
  bool at_checkpoint = false;
};

struct Landed {
  F32 impact = 0.0f;  // downward speed at the moment of contact
};

using GameEvent = std::variant<LanternLit, Fell, Respawned, Landed>;
using Events = std::vector<GameEvent>;

}  // namespace lantern::runtime
