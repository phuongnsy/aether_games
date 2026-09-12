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
#include "aether/anim/ragdoll.hpp"
#include "aether/physics/world.hpp"
#include "aether/ai/blackboard.hpp"
#include "aether/ai/perception.hpp"
#include "aether/nav/crowd.hpp"
#include "infiltration/features/guards/guards.hpp"
#include "infiltration/features/ragdoll/ragdoll.hpp"
#include "infiltration/runtime/player.hpp"
#include "infiltration/runtime/pose.hpp"

namespace infiltration::view {

using namespace aether;  // NOLINT(google-build-using-namespace)

// The space this draws, by the names content gives it.
using namespace content;   // NOLINT(google-build-using-namespace)
using namespace runtime;  // NOLINT(google-build-using-namespace)
using namespace features::guards;  // NOLINT(google-build-using-namespace)

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

// §16.6.3's ApplyRagDollsToSkeletons: the simulated bodies become a pose, in
// the limp body's own frame. `bodies` is caller-owned scratch for the same
// reason PoseBuffers is — this runs per downed guard per frame.
//
// A failed inverse leaves the previous pose alone rather than drawing a
// degenerate one: a frame of the last good pose beats a frame of nothing.
inline void PoseFromLimp(PoseBuffers& buf, std::vector<Transform>& bodies,
                         const resources::Skeleton& agent_rig,
                         const anim::RagdollRig& rig,
                         const physics::World& world,
                         const features::ragdoll::Limp& limp) {
    bodies.resize(limp.bodies.size());
    for (Usize b = 0; b < limp.bodies.size(); ++b) {
      bodies[b] = world.BodyTransform(limp.bodies[b]);
    }
    const std::optional<Mat4> from_world = TryInverse(LimpToWorld(limp));
    if (!from_world) {
      return;
    }
    buf.globals.resize(agent_rig.JointCount());
    anim::ApplyRagdollToPose(agent_rig, rig, bodies,
                             *from_world, buf.globals, buf.local);
}

// The guards: posed, masked by the posture their policy chose, and annotated
// with what they can see and what they remember. §17.2.3's test is whether a
// player can perceive the character's motivation, which is what the cone, the
// confidence bar and the memory cross are for.
//
// A downed guard is drawn from its BODIES when it has gone limp, and from the
// frozen pose when it has not — and gets no cone, no bar and no cross,
// because it has none of those any more.
constexpr Vec4 kGuardBone{0.95f, 0.55f, 0.35f, 1.0f};
constexpr Vec4 kCone{0.45f, 0.85f, 0.55f, 1.0f};
constexpr Vec4 kSure{0.35f, 1.00f, 0.45f, 1.0f};
constexpr Vec4 kMemory{0.95f, 0.65f, 0.25f, 1.0f};
constexpr Vec4 kDownBone{0.42f, 0.42f, 0.46f, 1.0f};

inline void DrawGuards(RenderFrame& frame, PoseBuffers& buf,
                       std::vector<Transform>& bodies,
                       const resources::Skeleton& clip_rig,
                       const resources::Skeleton& agent_rig,
                       std::span<const anim::LocomotionGait> gaits,
                       const anim::RetargetMap& map,
                       const anim::RagdollRig& rig,
                       const physics::World& world,
                       const features::guards::GuardState& guard,
                       std::span<const features::ragdoll::Limp> rag,
                       const ai::AgentBoards& boards, const nav::Crowd& crowd,
                       const ai::VisionCone& cone,
                       std::span<const Transform> alert_pose,
                       std::span<const F32> alert_mask) {
    if (gaits.empty()) {
      return;
    }
    for (Usize g = 0; g < kGuards; ++g) {
      // A DOWNED GUARD IS DRAWN FROM THE SAME FROZEN STATE — the phase and the
      // locomotion state stopped advancing, so `PoseFor` reproduces its last
      // pose exactly. Which is the point: it stands up straight, in mid-stride,
      // and that is what a ragdoll would fix.
      PoseFor(buf, clip_rig, agent_rig, gaits, map,
              guard.loco[g], guard.phase[g]);
      // §12.10.2.5's MASKED gesture layer, at the weight the POLICY chose.
      const F32 posture = static_cast<F32>(std::clamp(
          boards.Agent(g)->GetNumber(StringId{"posture"}, 0.0), 0.0, 1.0));
      if (posture > 0.0f && !alert_pose.empty()) {
        anim::AddPose(buf.local, alert_pose, posture, buf.local, alert_mask);
      }
      // A LIMP GUARD IS POSED FROM ITS BODIES (§16.6.3's
      // ApplyRagDollsToSkeletons), and drawn in the frame frozen at the
      // takedown — the simulation's displacement lands in the pelvis's local
      // pose, so a fixed placement is correct and a chasing one would not be.
      if (rag[g].live) {
        PoseFromLimp(buf, bodies, agent_rig, rig,
                     world, rag[g]);
        DrawSkeleton(frame, agent_rig, buf.local, buf.globals, rag[g].at, rag[g].facing, kDownBone);
        continue;
      }
      const Vec3 at = crowd.AgentPosition(guard.id[g]);
      DrawSkeleton(frame, agent_rig, buf.local, buf.globals, at, guard.loco[g].facing,
                   guard.disabled[g] ? kDownBone : kGuardBone);
      if (guard.disabled[g]) {
        continue;  // no cone, no confidence bar, no memory cross: it has none
      }

      // The cone, the confidence, and the remembered position — §17.2.3's own
      // test is whether a player can perceive the character's motivation.
      const Vec3 eye = at + Vec3{0.0f, 1.6f, 0.0f};
      constexpr int kArc = 9;
      Vec3 previous{};
      for (int k = 0; k <= kArc; ++k) {
        const F32 t = static_cast<F32>(k) / static_cast<F32>(kArc);
        const F32 a =
            guard.loco[g].facing + (t * 2.0f - 1.0f) * cone.half_angle;
        const Vec3 rim =
            eye + Vec3{std::sin(a), 0.0f, std::cos(a)} * cone.range;
        if (k == 0 || k == kArc) {
          frame.debug.AddLine(eye, rim, kCone);
        }
        if (k > 0) {
          frame.debug.AddLine(previous, rim, kCone);
        }
        previous = rim;
      }
      const ai::Awareness& aw = guard.aware[g];
      if (aw.confidence > 0.0f) {
        const Vec3 base = at + Vec3{0.0f, 2.0f, 0.0f};
        frame.debug.AddLine(base, base + Vec3{0.0f, aw.confidence, 0.0f},
                            aw.visible_now ? kSure : kMemory);
      }
      if (!aw.visible_now && aw.seconds_since_seen >= 0.0f) {
        const Vec3 m = aw.last_known_position;
        frame.debug.AddLine(m - Vec3{0.4f, 0, 0}, m + Vec3{0.4f, 0, 0},
                            kMemory);
        frame.debug.AddLine(m - Vec3{0, 0, 0.4f}, m + Vec3{0, 0, 0.4f},
                            kMemory);
      }
    }
}
}  // namespace infiltration::view
