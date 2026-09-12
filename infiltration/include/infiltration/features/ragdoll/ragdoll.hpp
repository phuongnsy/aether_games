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
#include <algorithm>
#include <span>

#include "aether/anim/ragdoll.hpp"
#include "aether/core/math/transform.hpp"
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


// A guard that is still conscious: its bodies are HELD on the pose the
// animation produced, so the hit boxes it is shot at are where it looks.
inline void HoldOnTargets(Limp& limp, physics::World& world,
                          std::span<const Transform> targets, F32 dt) {
  for (Usize b = 0; b < limp.bodies.size(); ++b) {
    // 13.5.1.2's impulse move, NOT a teleport. The velocity it leaves
    // behind is what a limb inherits when the motion type switches.
    (void)world.MoveBody(limp.bodies[b], targets[b], dt);
  }
}

// How long the takedown's authority takes to reach zero.
constexpr F32 kLimpFadeSeconds = 0.35f;

// A guard that is not conscious: §13.4.8.8's authority, draining. The joints
// are still driven toward the animated pose at a motor scale that fades,
// which is what makes a takedown slump rather than switch.
inline void DriveLimp(Limp& limp, physics::World& world,
                      const anim::RagdollRig& rig,
                      std::span<const Transform> targets, F32 dt) {
  const std::span<const anim::RagdollBone> bones = rig.Bones();
  Usize joint = 0;
  for (Usize b = 0; b < bones.size(); ++b) {
    const Usize parent = bones[b].parent_bone;
    if (parent == anim::RagdollBone::kNoBone ||
        joint >= limp.joints.size()) {
      continue;
    }
    const Quat relative =
        Conjugate(targets[parent].rotation) * targets[b].rotation;
    (void)world.SetConstraintTarget(limp.joints[joint], relative);
    (void)world.SetConstraintMotorScale(limp.joints[joint],
                                              limp.power);
    ++joint;
  }
  // §13.5.3.8: "a simple LERP blend between animation-generated and
  // physics-generated poses usually doesn't work very well, because the
  // physics pose very quickly diverges … As such, we may want to use
  // powered constraints during the transition." So authority DRAINS rather
  // than the pose being blended, and the body is holding its own animated
  // pose at the moment it starts to let go.
  limp.power = std::max(0.0f, limp.power - dt / kLimpFadeSeconds);
}
}  // namespace infiltration::features::ragdoll
