// The immutable per-frame contract between sim and view (AGENTS.md):
// `view` and the render passes consume THIS, never live game state.
#pragma once

#include <vector>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "tideworn/features/voyage/components.hpp"

namespace tideworn::runtime {

// One fish, ready for the creature view: pose from pos + vel (heading), the
// species picks scale. `phase` staggers the swim clip so a school never
// strokes in lockstep.
struct FishInstance {
  aether::Vec3 pos;
  aether::Vec3 vel;
  aether::U8 species = 0;
  aether::F32 phase = 0.0f;
};

struct HudData {
  aether::F32 wind_speed = 0.0f;
  aether::F32 time_of_day = 0.0f;
  voyage::Band band = voyage::Band::kCalm;
};

struct ViewSnapshot {
  aether::F32 clock_s = 0.0f;  // the wave field's deterministic clock
  aether::F32 wind_speed = 5.0f;
  aether::F32 time_of_day = 0.0f;
  voyage::Band band = voyage::Band::kCalm;
  // The boat's LOGICAL pose: xz position + heading. Riding the wave surface
  // (height/tilt) is presentation and stays view-side, sampled from the same
  // oracle the shader draws.
  aether::Vec2 boat_pos{0.0f, 0.0f};
  aether::F32 boat_heading = 0.0f;
  std::vector<FishInstance> fish;
  HudData hud;
};

}  // namespace tideworn::runtime
