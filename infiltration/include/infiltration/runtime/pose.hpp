// §12.9's pose evaluation: sample a gait, blend the two the solver chose,
// retarget onto the skeleton that will be used.
//
// USED, not drawn — which is why this is runtime and not view. It was in the
// view layer until the ragdoll's step needed it: PoseToRagdollTargets drives
// simulated bodies toward this pose, and a sim layer may not depend on a
// render one. The draws read it from here, which the law allows.
#pragma once

#include <cmath>
#include <span>
#include <vector>

#include "aether/anim/locomotion.hpp"
#include "aether/anim/pose.hpp"
#include "aether/anim/ragdoll.hpp"
#include "aether/anim/retarget.hpp"
#include "aether/core/math/mat.hpp"
#include "aether/core/math/transform.hpp"
#include "aether/core/types.hpp"
#include "aether/resources/clip.hpp"
#include "aether/resources/skeleton.hpp"

namespace infiltration::runtime {

using namespace aether;  // NOLINT(google-build-using-namespace)

// The scratch a posed character needs: sampled clip, the second gait it is
// blending with, the retargeted local pose, and the globals DrawSkeleton
// composes. Owned by the caller — this runs once per character per frame.
struct PoseBuffers {
  std::vector<Transform> clip;
  std::vector<Transform> blend;
  std::vector<Transform> local;
  std::vector<Mat4> globals;
};

// A gait at a phase, or a rest pose when the gait has no clip — the fallback
// matters: a missing clip must not leave the previous character's pose in the
// buffer.
inline void SampleGait(const resources::Skeleton& clip_rig,
                       const anim::LocomotionGait& gait, F32 phase,
                       std::vector<Transform>& into) {
    if (gait.clip == nullptr || gait.clip->Duration() <= 0.0f) {
      into.assign(clip_rig.JointCount(), Transform{});
      return;
    }
    anim::SampleClip(*gait.clip, std::fmod(phase, gait.clip->Duration()), into);
}

// §12.9 stages 1 and 3: sample, blend the two gaits the solver chose, then
// retarget onto the skeleton that will be drawn.
inline void PoseFor(PoseBuffers& buf, const resources::Skeleton& clip_rig,
                    const resources::Skeleton& agent_rig,
                    std::span<const anim::LocomotionGait> gaits,
                    const anim::RetargetMap& map,
                    const anim::LocomotionState& loco, F32 phase) {
    buf.clip.resize(clip_rig.JointCount());
    SampleGait(clip_rig, gaits[loco.gait_a], phase, buf.clip);
    if (loco.gait_b != loco.gait_a && loco.blend > 0.0f) {
      buf.blend.resize(buf.clip.size());
      SampleGait(clip_rig, gaits[loco.gait_b], phase, buf.blend);
      anim::BlendPoses(buf.clip, buf.blend, loco.blend, buf.clip);
    }
    buf.local.resize(agent_rig.JointCount());
    anim::RetargetPose(buf.clip, map, 1.0f, buf.local);
}


// §16.6.3 in the other direction: what the ANIMATION says each body should
// be, in the frame the character occupies. Called for a guard that is still
// conscious — its bodies are driven toward these — and for hit boxes, which
// want the same transforms for a different reason.
inline void PoseRagdollTargets(PoseBuffers& buf, const anim::RagdollRig& rig,
                               const resources::Skeleton& clip_rig,
                               const resources::Skeleton& agent_rig,
                               std::span<const anim::LocomotionGait> gaits,
                               const anim::RetargetMap& map,
                               const anim::LocomotionState& loco, F32 phase,
                               const Mat4& to_world,
                               std::vector<Transform>& targets) {
    PoseFor(buf, clip_rig, agent_rig, gaits, map, loco, phase);
    buf.globals.resize(agent_rig.JointCount());
    anim::ComposeGlobals(agent_rig, buf.local, buf.globals);
    targets.resize(rig.BoneCount());
    anim::PoseToRagdollTargets(rig, buf.globals, to_world,
                               targets);
}
}  // namespace infiltration::runtime
