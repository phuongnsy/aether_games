// A guard that has stopped being conscious: the bodies §13.4 simulates, and
// the frame they live in.
//
// GEA §12.9 stage 5 waited on a consumer since Phase E; the takedown is it.
// A FEATURE layer, and a sim one — physics links core alone, so nothing here
// can reach a renderer. Posing a skeleton FROM these bodies is the view's
// half and lives there.
#pragma once

#include <vector>

#include "aether/core/math/mat.hpp"
#include "aether/core/math/quat.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/physics/world.hpp"

namespace infiltration::features::ragdoll {

using namespace aether;  // NOLINT(google-build-using-namespace)

struct Limp {
  std::vector<physics::BodyId> bodies;
  std::vector<physics::ConstraintId> joints;
  Vec3 at{};  // the frame the whole body lives in, frozen at takedown
  F32 facing = 0.0f;
  F32 power = 0.0f;  // §13.4.8.8 authority, draining to zero
  bool live = false;
  // h4 — where the pelvis was, and when, at the instant the motion type
  // switched. The body's inherited SPEED is measured from these, and it is
  // the one observable that separates §13.5.1.2's impulse move from a
  // teleport (`event:2026-09-10#3`).
  Vec3 limp_from{};
  U64 limp_step = 0;
};

// Where the limp body's own frame sits in the world. Frozen at the takedown:
// the simulation's displacement lands in the pelvis's local pose, so a fixed
// placement is correct here and a chasing one would double the motion.
[[nodiscard]] inline Mat4 LimpToWorld(const Limp& limp) {
    return MakeTranslation(limp.at) *
           QuatToMat4(
               QuatFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, limp.facing));
}

}  // namespace infiltration::features::ragdoll
