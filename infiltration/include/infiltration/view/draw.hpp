// The slice drawn: debug geometry for the things that have no mesh — the
// level's triangles, the objective marker and the authored cover points.
//
// THE VIEW LAYER is the only one allowed to name aether::scene or
// aether::render, and these three need neither: a RenderFrame and the data
// they describe is the whole input. The scene graph and the camera come in a
// later cut, where a mistake moves a capture digest rather than failing to
// compile.
#pragma once

#include <cmath>

#include "aether/core/math/vec.hpp"
#include <span>
#include <vector>

#include <cmath>
#include "aether/anim/locomotion.hpp"
#include "aether/anim/pose.hpp"
#include "aether/anim/retarget.hpp"
#include "aether/resources/clip.hpp"
#include "aether/core/math/mat.hpp"
#include "aether/core/math/quat.hpp"
#include "aether/core/math/transform.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/resources/skeleton.hpp"
#include "aether/scene_core/camera_collision.hpp"
#include "aether/scene_core/orbit_component.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/core/types.hpp"
#include "infiltration/content/level.hpp"
#include "infiltration/runtime/player.hpp"

namespace infiltration::view {

using namespace aether;  // NOLINT(google-build-using-namespace)

// The space this draws, by the names content gives it.
using namespace content;  // NOLINT(google-build-using-namespace)

// The colours the slice draws with, MOVED from the app file rather than
// written afresh — a first version of this header invented four plausible
// values and all four capture digests drifted, which is the whole reason
// those oracles exist.
constexpr Vec4 kFloorLine{0.22f, 0.26f, 0.30f, 1.0f};
constexpr Vec4 kWallLine{0.55f, 0.45f, 0.35f, 1.0f};
constexpr Vec4 kCoverMark{0.55f, 0.60f, 0.95f, 1.0f};
constexpr Vec4 kGoalMark{0.95f, 0.90f, 0.40f, 1.0f};

inline void DrawLevel(RenderFrame& frame, const content::Level& level_) {
    for (Usize i = 0; i + 2 < level_.indices.size(); i += 3) {
      const Vec3 a = level_.vertices[level_.indices[i]];
      const Vec3 b = level_.vertices[level_.indices[i + 1]];
      const Vec3 c = level_.vertices[level_.indices[i + 2]];
      const Vec4 colour =
          (a.y > 0.01f || b.y > 0.01f || c.y > 0.01f) ? kWallLine : kFloorLine;
      frame.debug.AddLine(a, b, colour);
      frame.debug.AddLine(b, c, colour);
      frame.debug.AddLine(c, a, colour);
    }
}

inline void DrawObjective(RenderFrame& frame, int leg_) {
    const Vec3 goal = leg_ == 1 ? kObjective : kExtraction;
    constexpr F32 kTall = 1.8f;
    for (const F32 a : {0.0f, 1.5708f}) {
      const Vec3 arm{std::cos(a) * 0.5f, 0.0f, std::sin(a) * 0.5f};
      frame.debug.AddLine(goal - arm, goal + Vec3{0.0f, kTall, 0.0f},
                          kGoalMark);
      frame.debug.AddLine(goal + arm, goal + Vec3{0.0f, kTall, 0.0f},
                          kGoalMark);
      frame.debug.AddLine(goal - arm, goal + arm, kGoalMark);
    }
}

inline void DrawCover(RenderFrame& frame) {
    for (const Vec3& c : kCoverPoints) {
      frame.debug.AddLine(c, c + Vec3{0.0f, 0.6f, 0.0f}, kCoverMark);
      frame.debug.AddLine(c - Vec3{0.3f, 0, 0}, c + Vec3{0.3f, 0, 0},
                          kCoverMark);
    }
}


// A posed skeleton as bones: one line per joint, from its parent to it. The
// root has no parent, so it draws a stub downward — which is what makes a
// floating character read as standing rather than as a cloud of lines.
//
// `globals` is scratch the caller owns: this runs twice a frame per character
// and a local vector would allocate on every one of them.
inline void DrawSkeleton(RenderFrame& frame, const resources::Skeleton& rig,
                         std::span<const Transform> local,
                         std::vector<Mat4>& globals, Vec3 at, F32 facing,
                         Vec4 colour) {
        globals.resize(rig.JointCount());
    anim::ComposeGlobals(rig, local, globals);
    const Mat4 place = MakeTranslation(at) *
                       QuatToMat4(QuatFromAxisAngle(Vec3{0, 1, 0}, facing));
    const std::span<const resources::Joint> joints = rig.Joints();
    for (Usize j = 0; j < joints.size() && j < globals.size(); ++j) {
      const Vec3 here = TransformPoint(place * globals[j], Vec3{});
      const Vec3 from =
          joints[j].parent < 0
              ? here + Vec3{0, -0.12f, 0}
              : TransformPoint(
                    place * globals[static_cast<Usize>(joints[j].parent)],
                    Vec3{});
      frame.debug.AddLine(from, here, colour);
    }
}

constexpr scene::CameraCollisionParams kCameraProbe{.radius = 0.35f,
                                                    .min_distance = 1.0f};

// What the probe has seen. Counters rather than a pass/fail, because the
// measurement came FIRST: the camera's centre was never inside a solid, so
// 'never clips geometry' would have been a gate that could not fail, while
// route 2 spent 36% of its run with the wall between camera and player.
struct CameraProbe {
  U64 steps = 0;
  U64 occluded = 0;
  U64 inside = 0;
  F32 worst = 0.0f;
  U64 free = 0;
};

// GEA §13.5.3.7's camera collision: written every fixed step and read by
// OrbitComponent::Update AFTER its ease, which is what makes the pull-in
// immediate and the release smooth.
inline void StepCameraCollision(scene::OrbitComponent* orbit_,
                                const GeometryQuery3& walls_) {
    if (orbit_ == nullptr) {
      return;
    }
    // One step stale, the same lag `OrbitComponent` documents for a `target`:
    // this runs before `scene_.Update`, so the angles are last step's. At 6 rad
    // of stiffness that is under a degree.
    orbit_->distance_limit = scene::ResolveCameraDistance(
        orbit_->pivot, orbit_->CurrentDirection(), orbit_->distance, walls_,
        ~0U, kCameraProbe);
}

// The same camera, measured rather than resolved.
inline void ProbeCamera(CameraProbe& probe_, const scene::Node* node,
                        const scene::OrbitComponent* orbit_,
                        const GeometryQuery3& walls_) {
      if (node == nullptr || orbit_ == nullptr) {
      return;
    }
    const Vec3 at = node->local.position;
    ++probe_.steps;
    if (walls_.OverlapsSphere(Sphere{.center = at, .radius = 0.01f}, ~0U)) {
      ++probe_.inside;
    }
    const Vec3 pivot = orbit_->pivot;
    const Vec3 to = at - pivot;
    const F32 d = Length(to);
    if (d > 1e-4f) {
      const std::optional<GeometryHit3> hit =
          walls_.Raycast(Ray3{.origin = pivot, .direction = to / d}, ~0U);
      // THE TOLERANCE IS THE PROBE RADIUS, not zero. The resolver stops the
      // camera one radius short of the surface, and a surface exactly at the
      // camera would otherwise read as an overshoot of 0.
      if (hit && hit->t < d - kCameraProbe.radius * 0.5f) {
        ++probe_.occluded;
        probe_.worst = std::max(probe_.worst, d - hit->t);
      }
    }
    // And the camera must spend some of the run at the distance it ASKED for,
    // or a camera welded at `min_distance` would report zero occlusions while
    // staring at the player's shoulder for ten seconds.
    if (orbit_->CurrentDistance() > orbit_->distance - 0.05f) {
      ++probe_.free;
    }
}

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

// The player, drawn as bones — there is no character mesh in the slice, and a
// skeleton is what makes a gait legible.
constexpr Vec4 kPlayerBone{0.35f, 0.80f, 1.00f, 1.0f};

inline void DrawPlayer(RenderFrame& frame, PoseBuffers& buf,
                       const resources::Skeleton& clip_rig,
                       const resources::Skeleton& agent_rig,
                       std::span<const anim::LocomotionGait> gaits,
                       const anim::RetargetMap& map,
                       const runtime::PlayerState& player) {
    if (gaits.empty()) {
      return;
    }
    PoseFor(buf, clip_rig, agent_rig, gaits, map,
            player.loco, player.phase);
    DrawSkeleton(frame, agent_rig, buf.local, buf.globals, player.position, player.loco.facing, kPlayerBone);
}
}  // namespace infiltration::view
