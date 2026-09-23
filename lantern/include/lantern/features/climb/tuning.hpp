// Movement feel, living WITH its feature (AGENTS.md). Every number here
// was settled in examples/lab/controller_lab against its eight torture cases —
// they are measured defaults, not taste.
#pragma once

#include "aether/core/types.hpp"

namespace lantern::climb {

using aether::F32;

struct Tuning {
  // A sphere, because SphereCast is the only sweep the query layer offers —
  // there is no capsule or box sweep (controller_lab, 2026-08-08).
  F32 radius = 0.4f;
  F32 walk_speed = 4.2f;
  F32 gravity = -20.0f;
  F32 jump_speed = 7.2f;
  F32 step_up = 0.4f;
  // cos(50°): ground steeper than this is slid down rather than stood on.
  F32 ground_cos = 0.64f;
  // How far below the feet still counts as contact, so a small lip does not
  // read as a fall for one frame.
  F32 snap = 0.12f;
};

}  // namespace lantern::climb
