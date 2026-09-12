// A VERTICAL SLICE — the first non-lab consumer of five ADRs at once, and the
// engine's first 3D player.
//
// WHY THIS EXISTS AND WHAT IT IS NOT. It adds no engine capability. ADRs
// 0188–0192 all shipped with a LAB as their only consumer — per-subsystem
// memory, perception and memory, the `ai` decision tier, the audio filter
// stage, the blend mask and gesture layer — and ADR-0179 had already recorded
// "its only caller is a LAB rather than a game" as the weak point of the AI
// column's own argument. Five deep. Four consecutive plans also got their cost
// prediction wrong, which is what components that have never met a workload do.
// This is the workload.
//
// § GEA §17.2.1 CALLS PLAYER MECHANICS "the most important gameplay system",
// describes it as INTEGRATION — "human interface device systems, motion
// simulation, collision detection, animation and audio … the game camera,
// weapons, cover" — and then declines to specify any of it: "there's no one
// place you can go to learn all about them … Play games and try to
// reverse-engineer their player mechanics."
//
// THIS COMMENT THEN CONCLUDED THE BOOK WAS SILENT ON THE MOVEMENT ITSELF, ON
// THE STRENGTH OF A ZERO-HIT SEARCH FOR "character controller", AND IT WAS
// WRONG. §13.5.3.6 Character Mechanics specifies the whole thing — the
// mechanism ("sphere or capsule casts to probe in the direction of desired
// motion. Collisions are resolved manually") and five behaviours — and the
// phrase is invisible only because the corpus's OCR strips the spaces out of
// it. A grep for a phrase is not a consultation (event:2026-09-08#117).
//
// § SO THE MOVEMENT MODEL IS NO LONGER HERE. `nav::MoveCharacter` (ADR-0198)
// owns the sweep, the slide, the slope cutoff, the kerb and the ground snap;
// what stays at this call site is what a GAME owns — camera-relative intent,
// gaits, and the accel/brake feel. Everything else is wiring:
//
//   nav::MoveCharacter                the PLAYER moves         (ADR-0198)
//   nav::BakeNavMesh / nav::Crowd     the guards' route        (ADR-0179/0181)
//   ai::CanSee / ai::UpdateAwareness  they see THE PLAYER      (ADR-0189)
//   ai::Blackboard + a Lua policy     they decide             (ADR-0190)
//   anim::SolveLocomotion             the player's OWN gait    (ADR-0184)
//   anim::BuildJointMask + AddPose    their alert posture      (ADR-0192)
//   audio::Propagate + SetFilter      the player is HEARD      (ADR-0191)
//
// § AND IT IS A LOOP NOW (g1–g7). GEA §16.10 named the gap the first version
// left: a world that defines "how objects behave individually" and "says
// nothing of the player's objectives, what happens if he or she completes them,
// and what fate should befall the player if he or she fails." Its mechanism is
// "often implemented as a finite state machine", and `ui::ScreenStack` already
// IS one — so the flow below adds no engine code:
//
//   a detection meter        the guards' confidence, aggregated and DRAWN
//   an objective             reach the marker in their half, then get back
//   two terminal states      SPOTTED and EXTRACTED, on the screen stack
//   a retry                  §16.10's own rule, back to the start of the state
//   a takedown               a guard disabled from behind — and §12.9 stage 5's
//                            ragdoll trigger, fired with a named consumer
//
// § WHAT TO LOOK FOR. Walk out from behind the wall: a guard's cone finds you,
// its confidence bar fills, its posture changes, and it breaks for cover. Step
// back behind the wall and the sound of you goes muffled rather than merely
// quiet — that last one is the half a screenshot cannot show, which is why
// `--autopilot` asserts it as a number. Walk up behind a guard instead and it
// goes down — standing bolt upright, which is the finding g4 exists to produce.
//
// § RUN IT HEADLESS WITH `AETHER_BENCH_FRAMES`, NOT `--frames`. A headless
// `--frames 6000` produces about thirty fixed steps, because the fixed step is
// wall-clock driven (event:2026-09-08#5).
#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aether/ai/blackboard.hpp"
#include "aether/ai/cover.hpp"
#include "aether/ai/perception.hpp"
#include "aether/anim/joint_mask.hpp"
#include "aether/anim/locomotion.hpp"
#include "aether/anim/pose.hpp"
#include "aether/anim/ragdoll.hpp"
#include "aether/anim/retarget.hpp"
#include "aether/app/agent_bindings.hpp"
#include "aether/app/app.hpp"
#include "aether/app/command_line.hpp"
#include "aether/audio/audio_system.hpp"
#include "aether/audio/spatial.hpp"
#include "aether/core/geometry_query.hpp"
#include "aether/core/log.hpp"
#include "aether/core/math/mat.hpp"
#include "aether/input/edge_latch.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/nav/character.hpp"
#include "aether/nav/character_obstacles.hpp"
#include "aether/nav/crowd.hpp"
#include "aether/nav/navmesh.hpp"
#include "aether/physics/world.hpp"
#include "aether/resources/model.hpp"
#include "aether/resources/truetype.hpp"
#include "aether/scene_core/camera_collision.hpp"
#include "aether/scene_core/camera_component.hpp"
#include "aether/scene_core/orbit_component.hpp"
#include "aether/script/vm.hpp"
#include "aether/ui/context.hpp"
#include "aether/ui/screen.hpp"
#include "infiltration/content/level.hpp"
#include "infiltration/features/guards/guards.hpp"
#include "infiltration/features/ragdoll/ragdoll.hpp"
#include "infiltration/runtime/player.hpp"
#include "infiltration/view/draw.hpp"
#include "example_base.hpp"
#include "aether/core/log.hpp"

using namespace aether;
using namespace infiltration::content;
using namespace infiltration::features::guards;
using namespace infiltration::features::ragdoll;
using namespace infiltration::runtime;
using namespace infiltration::view;

namespace {

constexpr const char* kClipModel = "models/xbot_crowd.glb";
constexpr const char* kAgentModel = "models/human_0.glb";
// The loop that stands in for the player making noise. `ambient.wav` is what
// the sandbox ships; a real game would have footsteps gated on the gait.
constexpr const char* kStepClip = "audio/ambient.wav";

constexpr Vec4 kGuardBone{0.95f, 0.55f, 0.35f, 1.0f};
constexpr Vec4 kCone{0.45f, 0.85f, 0.55f, 1.0f};
constexpr Vec4 kSure{0.35f, 1.00f, 0.45f, 1.0f};
constexpr Vec4 kMemory{0.95f, 0.65f, 0.25f, 1.0f};
constexpr Vec4 kDownBone{0.42f, 0.42f, 0.46f, 1.0f};

// --- the HUD's palette (g1/g2) ----------------------------------------------
constexpr Vec4 kHudBack{0.06f, 0.07f, 0.09f, 0.85f};
// The meter's empty TRACK, lighter than the panel: at zero exposure the bar is
// all track, and a track the colour of the sky is a meter you cannot find.
constexpr Vec4 kHudTrack{0.20f, 0.22f, 0.26f, 0.90f};
constexpr Vec4 kHudText{0.80f, 0.84f, 0.86f, 1.0f};
constexpr Vec4 kScrim{0.02f, 0.02f, 0.03f, 0.70f};
constexpr Vec4 kMeterCalm{0.30f, 0.80f, 0.45f, 1.0f};
constexpr Vec4 kMeterWary{0.95f, 0.75f, 0.25f, 1.0f};
constexpr Vec4 kMeterHot{0.95f, 0.25f, 0.25f, 1.0f};

// A bar that only changes LENGTH reads as a number; one that changes COLOUR
// reads as danger. `vec.hpp` has no Vec4 lerp and one call site is not a case
// for adding one.
[[nodiscard]] constexpr Vec4 Mix(Vec4 a, Vec4 b, F32 t) {
  return Vec4{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
              a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

// --- the player --------------------------------------------------------------

// §14.4.5.3's four states as words. `spatial.hpp` exports no such helper and
// `audio_probe` carries its own; a third copy would be the moment to promote
// it.
[[nodiscard]] const char* StateName(audio::PathState s) {
  switch (s) {
    case audio::PathState::kFree:
      return "free";
    case audio::PathState::kOccluded:
      return "occluded";
    case audio::PathState::kObstructed:
      return "obstructed";
    case audio::PathState::kExcluded:
      return "excluded";
  }
  return "?";
}


// --- the loop's own numbers (g1–g4) ------------------------------------------
// §17.2.1 declines to specify player mechanics, so none of these is cited.
// `platformer.cpp` is this engine's precedent for tuning a controller by hand,
// and the plan's own risk list predicted that a detection meter would need the
// same treatment: a loop is the first thing here that can be UNFAIR rather than
// merely wrong.


constexpr F32 kArriveRadius = 1.2f;

// Both guards' spawns, named because a RETRY has to put them back (§16.10).
constexpr std::array<Vec3, kGuards> kGuardSpawn{Vec3{-7.0f, 0.0f, -6.0f},
                                                Vec3{-7.0f, 0.0f, 6.0f}};

// SEEN IS NOT CAUGHT, and this is the fairness call the plan warned about.
// ADR-0189's default gain saturates confidence in half a second of sight, so a
// catch on confidence alone would end the run from across the map and make the
// meter pointless. A guard has to be ON you.
constexpr F32 kCatchRange = 2.5f;
constexpr F32 kCatchConfidence = 0.9f;

// The takedown: close, and from BEHIND — the dot product `ai::CanSee` already
// uses, one direction round.
constexpr F32 kTakedownRange = 1.7f;
constexpr F32 kTakedownBehind = -0.35f;  // dot(guard forward, guard -> player)

// §17.2.2's follow camera "acts much like a look-at camera focused on the
// player character … but its motion typically LAGS the player". This is the
// lag, as FollowComponent's own frame-rate-independent 1-exp(-k*dt).
constexpr F32 kCameraStiffness = 6.0f;
// §13.5.3.7's camera probe. `near_z` is 0.05, so a 0.35 m probe leaves the near
// plane clear of a surface the camera has stopped short of.
//
// `min_distance` IS 1.0 BECAUSE THE LEVEL SAYS SO, and it was measured rather
// than chosen: at the component's old 3.0 the camera was still behind the wall
// for 123 of 401 steps of route 2, at 2.0 for 65, and the overshoot at 2.0 was
// 0.88 m — so the geometry demands about 1.12 m and anything above that leaves
// the camera stuck outside. §13.5.3.7's first bullet blesses going much
// further: "In a third-person game, you can zoom all the way in to a
// first-person view without causing too much trouble (other than making sure
// the camera doesn't interpenetrate the character's head in the process)."
//
// `probe_passes` is the default: the off-axis case was measured at ZERO steps
// here, so buying resolution for it would be paying for nothing.

// --- the ragdoll (r4/r5) -----------------------------------------------------
// §13.5.3.8's transition, as a number: full motor authority at the instant of
// the takedown, draining to nothing over this. Short, because a guard being
// choked out is not a slow collapse — and long enough that the body is holding
// its own pose rather than being dropped into one.
// N·m. Measured rather than guessed: 400 holds a 10 kg limb whose weight asks
// for about 42, and a position motor at a 60 Hz step cannot slew a limb it did
// not start on top of anyway (event:2026-09-08#94).
constexpr F32 kLimpTorque = 400.0f;
// How far the pelvis must still travel horizontally in the first 0.2 s of being
// limp. CALIBRATED, not chosen: see the h4 assertion for what it measures and
// docs/perf/hit-box-baseline.md for the figure it was set from.
constexpr F32 kInheritedTravel = 0.05f;
// How low the PELVIS must be a second after going limp. A standing one is about
// a metre up; see the assertion for the vacuous 0.25 m this replaced.
constexpr F32 kFallenPelvis = 0.5f;
// The shortest hit that counts as a REACH rather than a collision. Route 1
// lands its takedown at about 1.45 m of the 1.7 m range; a hit under this is
// the player standing inside the guard, which nothing currently prevents.
constexpr F32 kMinReach = 0.3f;
// The query mask a character's bodies answer on, so a raycast can tell a limb
// from the level. NOT a simulation filter — ADR-0180 applies `layer` at query
// time.
constexpr U32 kBodyLayer = 4;

// --- characters as obstacles (b1) --------------------------------------------
// §13.3.8.1's layer, on the movement capsules rather than on the hit boxes: "a
// collidable can be a member of one (and only one) collision layer". The
// player's `CharacterBody::mask` is all-ones, so it sees the level AND the
// characters; a consumer that wanted ghosts would drop this bit.
constexpr U32 kCharacterLayer = 8;
// The handles the self-exclusion matches on. Non-zero, because 0 means
// "unidentified" to `CharacterObstacles` and never matches.
constexpr U64 kPlayerCapsuleId = 1;
constexpr U64 kGuardCapsuleId = 100;
// The guards' own capsule, and it is `AgentParams::radius` deliberately: a
// guard that blocks at a different radius from the one its navmesh was eroded
// by can be pushed somewhere it cannot path out of.
constexpr F32 kGuardRadius = 0.35f;

// --- hit boxes (h2/h3) -------------------------------------------------------
// GEA §13.5.3.6, and it is the sentence the whole representation comes from:
// "we usually model characters as a set of game-driven capsule-shaped rigid
// bodies, each one linked to a joint in the character's animated skeleton.
// These bodies are primarily used for bullet hit detection or to generate
// secondary effects such as when a character's arm bumps an object off a
// table."
//
// THEY ARE SUPPOSED TO CLIP THE WALL, which is the trap for anyone writing an
// assertion against them: "Because these bodies are game-driven, they won't
// avoid interpenetrations with immovable objects in the physics world, so it is
// up to the animator to ensure that the character's movements appear
// believable." Keeping a character OUT of geometry is `nav::MoveCharacter`'s
// job and a different half of the same section.
//
// The id a hit box answers a raycast with: a tag so it cannot be confused with
// the floor (1) or the wall (2), then the guard and the bone. `user_id` is
// opaque to `physics::World` by contract, so this needs no seam change.
constexpr U64 kHitBoxTag = 0xB0'0000ull;
[[nodiscard]] constexpr U64 HitBoxId(Usize guard, Usize bone) {
  return kHitBoxTag | (static_cast<U64>(guard) << 8) | static_cast<U64>(bone);
}
[[nodiscard]] constexpr bool IsHitBox(U64 id) {
  return (id & 0xFF'0000ull) == kHitBoxTag;
}
[[nodiscard]] constexpr Usize HitBoxGuard(U64 id) {
  return static_cast<Usize>((id >> 8) & 0xFFull);
}
[[nodiscard]] constexpr Usize HitBoxBone(U64 id) {
  return static_cast<Usize>(id & 0xFFull);
}

// The autopilot's budgets, in seconds — a leg advances on ARRIVAL and falls
// back to these, so an unreachable waypoint costs its own budget rather than
// the whole run.
constexpr F32 kStalkBudget = 12.0f;
constexpr F32 kLegBudget = 14.0f;
constexpr F32 kLoiterSeconds = 2.5f;  // how long route 1 stands in front of the
                                      // guard it just put down (see g4)

// --- the flow ----------------------------------------------------------------
// §16.10: "we need some kind of system to control high-level game flow. This is
// often implemented as a finite state machine." `ui::ScreenStack` is that FSM
// and the engine already had it, so this whole section is a consumer.
//
// NOT built, and named because the book describes it: Naughty Dog's task graph,
// which "permits parallel tasks, where one task branches out into two or more
// parallel tasks". Three states and one retry is the scope.
enum class Outcome : U8 { kRunning, kCaught, kCompleted };

class Infiltration;
using FlowScreen = ui::Screen<const app::AppContext>;
using FlowStack = ui::ScreenStack<const app::AppContext>;

// The live run. Only the TOP screen updates, so pushing a terminal screen
// freezes the world for free — which is the reason the flow is a stack rather
// than an enum on the game.
class PlayingScreen final : public FlowScreen {
 public:
  explicit PlayingScreen(Infiltration& game) : game_(game) {}
  void Update(const app::AppContext& ctx, F32 dt) override;
  void BuildUi(const app::AppContext& ctx, ui::Context& ui) override;

 private:
  Infiltration& game_;
  // 0..N fixed steps run per frame but transitions apply ONCE, so without this
  // a run that ends mid-frame pushes its terminal screen several times.
  bool handed_over_ = false;
};

// BOTH terminal states, one class: they differ in their words and their colour,
// and §16.10's transition rule — "often, failure sends the player back to the
// beginning of the current state, so he or she can try again" — is the same
// button on either. Pushed rather than Replaced, so the retry is a Pop onto the
// run that is still underneath.
class OutcomeScreen final : public FlowScreen {
 public:
  OutcomeScreen(Infiltration& game, bool won) : game_(game), won_(won) {}
  void HandleInput(const app::AppContext& ctx) override;
  void Update(const app::AppContext& ctx, F32 dt) override;
  void BuildUi(const app::AppContext& ctx, ui::Context& ui) override;

 private:
  void Retry();
  Infiltration& game_;
  bool won_ = false;
  F32 shown_ = 0.0f;
  bool leaving_ = false;
};

class Infiltration final : public examples::ExampleGame {
 public:
  explicit Infiltration(int autopilot) : autopilot_(autopilot) {}

  Result<void> Load(const app::AppContext& ctx) override {
    auto clips = ctx.resources.Load<resources::Model>(kClipModel);
    if (!clips) {
      return std::unexpected(clips.error());
    }
    auto agent = ctx.resources.Load<resources::Model>(kAgentModel);
    if (!agent) {
      return std::unexpected(agent.error());
    }
    clips_ = *clips;
    agent_ = *agent;

    level_ = MakeLevel();
    auto mesh = nav::BakeNavMesh(
        {.vertices = level_.vertices, .indices = level_.indices},
        nav::NavParams{
            .agent_radius = 0.35f, .cell_size = 0.15f, .cell_height = 0.15f});
    if (!mesh) {
      return std::unexpected(mesh.error());
    }
    mesh_ = std::move(*mesh);

    auto crowd = nav::Crowd::Create(mesh_, 8, 1.0f);
    if (!crowd) {
      return std::unexpected(crowd.error());
    }
    crowd_ = std::move(*crowd);
    if (auto spawned = SpawnGuards(); !spawned) {
      return std::unexpected(spawned.error());
    }

    // --- the rig, the gaits and the retarget (ADR-0178/0184) ----------------
    const resources::Skeleton& rig = agent_->Skeleton();
    const auto rest_of = [](const resources::Model& m,
                            std::vector<Transform>& into) {
      into.assign(m.Skeleton().JointCount(), Transform{});
      const resources::Clip* ref = m.FindClip("idle");
      if (ref == nullptr && !m.Clips().empty()) {
        ref = &m.Clips()[0];  // the generated humans carry no clip called idle
      }
      if (ref != nullptr) {
        anim::SampleClip(*ref, 0.0f, into);
      }
    };
    rest_of(*clips_, clip_rest_);
    rest_of(*agent_, agent_rest_);
    // THE REST POSES ARE NOT OPTIONAL, and the first version of this file left
    // them out — which is how it drew its player and both guards EIGHTY-FOUR
    // METRES IN THE AIR from the day it landed (event:2026-09-08#54).
    // `retarget.hpp` documents this exact failure in writing, measured on this
    // exact rig pair: the bind derived from `inverse_bind` is in world metres
    // while the clip is in armature units, "so differencing them is nonsense
    // and the target flew 80 m off-screen". Every assertion in this slice is a
    // NUMBER, none of them looks at the picture, and so nothing noticed.
    map_ = anim::BuildRetargetMap(clips_->Skeleton(), rig,
                                  anim::MixamoToMakeHuman(), clip_rest_,
                                  agent_rest_);
    if (map_.MappedCount() == 0) {
      return Fail(Errc::kInitFailed, "infiltration: nothing retargeted");
    }

    for (const char* name : {"idle", "walk", "run"}) {
      if (const resources::Clip* c = clips_->FindClip(name)) {
        const F32 authored =
            name[0] == 'i' ? 0.0f : (name[0] == 'w' ? 1.4f : 3.6f);
        gaits_.push_back({.clip = c, .authored_speed = authored});
      }
    }
    if (gaits_.empty()) {
      return Fail(Errc::kInitFailed, "infiltration: no locomotion clips");
    }

    // --- the guards' alert posture: §12.10.2.5's masked gesture layer -------
    alert_pose_.assign(agent_rest_.size(), Transform{});
    if (const resources::Clip* sneak = clips_->FindClip("sneak_pose");
        sneak != nullptr && !agent_rest_.empty()) {
      std::vector<Transform> on_clip(clips_->Skeleton().JointCount());
      // AT THE END, not at t=0: a `_pose` clip holds REST in its first keyframe
      // and the pose in its last (ADR-0192 found this the hard way).
      anim::SampleClip(*sneak, sneak->Duration(), on_clip);
      std::vector<Transform> on_agent = agent_rest_;
      anim::RetargetPose(on_clip, map_, 1.0f, on_agent);
      anim::MakeDifferencePose(on_agent, agent_rest_, alert_pose_);
    }
    alert_mask_.assign(rig.JointCount(), 0.0f);
    if (const auto spine = rig.Find(StringId::FromRuntime("spine03"))) {
      (void)anim::BuildJointMask(rig, *spine, alert_mask_,
                                 {.ancestor_falloff = 0.5f});
    } else {
      return Fail(Errc::kInitFailed, "infiltration: no spine03 on this rig");
    }

    // --- the characters' physics world (r4, widened by h2) ------------------
    // IT STOPPED BEING THE RAGDOLL'S PRIVATE WORLD ON 2026-09-10. It held the
    // floor, the wall and whatever was limp; it now holds every guard's hit
    // boxes as well, from spawn, and those same bodies ARE the ragdoll when one
    // goes down (GEA §13.5.1.4's runtime motion-type switch). One set of bodies
    // per character rather than two is what ADR-0196 deferred by name.
    //
    // It is still not the LEVEL's world, and that is still a scoping decision
    // rather than a design one. GEA §13.3.2.1 says physics and collision share
    // ONE world and ADR-0180 built `physics::World` that way — but this slice's
    // level is a triangle soup behind a hand-written `WallQuery`, not physics
    // bodies, so the floor and the wall here are duplicates of geometry the
    // character controller queries elsewhere. The moment the level itself
    // becomes physics geometry that duplication goes away, which is the
    // direction the seam already points.
    if (auto world = physics::World::Create()) {
      body_world_ = std::move(*world);
      // The ground a body lands on...
      if (!body_world_.AddBody(
              {.transform = {.position = Vec3{0.0f, -0.5f, 0.0f}},
               .shape = physics::Shape::Box(Vec3{kHalf, 0.5f, kHalf}),
               .mass = 0.0f,
               .friction = 0.8f,
               .user_id = 1})) {
        LogWarn("infiltration: no floor for the limp world");
      }
      // ...and the wall, so a guard taken down against it does not sink through
      // the one piece of cover in the level.
      if (!body_world_.AddBody(
              {.transform = {.position = Vec3{(kWallEnd - kHalf) * 0.5f,
                                              kWallTop * 0.5f, 0.0f}},
               .shape = physics::Shape::Box(
                   Vec3{(kWallEnd + kHalf) * 0.5f, kWallTop * 0.5f, kWallZ}),
               .mass = 0.0f,
               .user_id = 2})) {
        LogWarn("infiltration: no wall for the limp world");
      }
    } else {
      LogWarn("infiltration: no limp world — the takedown will stay stiff");
    }
    if (auto built = anim::BuildRagdollRig(rig, anim::HumanRagdoll())) {
      rag_rig_ = std::move(*built);
    } else {
      // REFUSED RATHER THAN DEGRADED, which is what BuildRagdollRig is for: a
      // rig missing a femur is a hole in a body, and a silently partial ragdoll
      // is the `nav_lab` gesture-layer failure again.
      return std::unexpected(built.error());
    }

    // --- the policy (ADR-0190) ---------------------------------------------
    auto vm = script::Vm::Create(script::VmConfig{
        .sandbox = script::Sandbox::kDeterministic, .memory_budget = 1 << 20});
    if (!vm) {
      return std::unexpected(vm.error());
    }
    policy_ = std::move(*vm);
    bindings_ = std::make_unique<app::AgentBindings>(
        app::AgentBindingConfig{.boards = &boards_, .world = &walls_});
    if (auto bound = bindings_->Bind(*policy_); !bound) {
      return std::unexpected(bound.error());
    }
    if (auto loaded = policy_->Load("infiltration_policy.lua", kPolicySource);
        !loaded) {
      return std::unexpected(loaded.error());
    }
    if (auto sealed = policy_->SealGlobals(); !sealed) {
      return std::unexpected(sealed.error());
    }
    if (!policy_->Has("agent_policy")) {
      return Fail(Errc::kInvalidArgument, "infiltration: no agent_policy");
    }

    // --- the camera: §17.2.2's LOOK-AT camera, "rotates about a target point
    //     and can be moved in and out relative to this point". Its pivot
    //     tracks the player, which is what makes the movement below
    //     camera-relative.
    const scene::NodeId camera = scene_.CreateNode(scene_.Root());
    camera_node_ = camera;
    auto* view = scene_.AddComponent<scene::CameraComponent>(camera);
    view->projection = ProjectionMode::kPerspective;
    view->reference_size = 0.0f;
    view->near_z = 0.05f;
    view->far_z = 200.0f;
    scene_.SetActiveCamera(camera);
    orbit_ = scene_.AddComponent<scene::OrbitComponent>(camera);
    orbit_->pivot = Vec3{0.0f, 1.2f, 0.0f};
    orbit_->distance = 9.0f;
    orbit_->min_distance = 1.0f;  // see kCameraProbe: the level demands it
    orbit_->max_distance = 40.0f;
    orbit_->pitch = 0.42f;
    camera_pivot_ = kStart + Vec3{0.0f, 1.2f, 0.0f};  // g5 starts settled

    // --- THE GUARDS ARE AUDIBLE (ADR-0191) ----------------------------------
    // A looping source per GUARD, with the listener at the camera. The first
    // version put the source on the PLAYER and the listener on the camera —
    // which follows the player — so the propagation measured player-to-camera
    // and was clear by construction. An occlusion model whose two ends move
    // together can never occlude anything.
    //
    // This way round is also the physically right one: you hear a guard behind
    // the wall MUFFLED rather than merely quiet, which is the distinction the
    // filter stage exists to make and which nothing outside a probe has used.
    audio_ = &ctx.audio;
    if (auto clip = ctx.resources.Load<resources::AudioClip>(kStepClip)) {
      step_clip_ = *clip;
      for (Usize g = 0; g < kGuards; ++g) {
        voices_[g] = audio_->PlaySpatial(
            step_clip_,
            audio::SpatialSource{.position = crowd_.AgentPosition(guard_.id[g]),
                                 .falloff_min = 1.5f,
                                 .falloff_max = 26.0f,
                                 .gain = 0.6f},
            /*loop=*/true);
      }
    } else {
      LogInfo("infiltration: no '{}' — the audible half is inert", kStepClip);
    }

    // --- the HUD (g1/g2) ----------------------------------------------------
    // A null font is legal: `ui::Context` skips text, so panels — the meter
    // itself — still draw and a headless run still exercises the layout.
    white_ = examples::MakeUiWhite(ctx.device);
    if (!white_) {
      return Fail(Errc::kInitFailed, "infiltration: no ui texel");
    }
    if (auto bytes = ctx.assets.Read("fonts/debug.ttf")) {
      if (auto f = resources::LoadTrueTypeFont(
              ctx.device, std::span<const Byte>(bytes->data(), bytes->size()),
              24)) {
        font_ = std::make_shared<const resources::Font>(std::move(*f));
      } else {
        LogWarn("infiltration: font: {}", f.error().message);
      }
    }

    // h2 — after the rig, the gaits and the retarget, because a hit box is
    // placed from the animated pose and none of that exists earlier in Load.
    BuildHitBoxes();

    BuildPanel();
    // §16.10's FSM starts in its first state. Applied immediately, so the very
    // first FixedUpdate has a top screen to step — a queued-but-unapplied push
    // would silently drop a frame of simulation.
    screens_.Push(std::make_unique<PlayingScreen>(*this));
    screens_.ApplyPending(ctx);
    LogInfo(
        "infiltration: {} navmesh polygons, {} guards, mask {} joints — walk "
        "out from behind the wall and they will find you; walk up BEHIND one "
        "and it goes down (E)",
        mesh_.PolygonCount(), kGuards,
        std::ranges::count_if(alert_mask_, [](F32 w) { return w > 0.0f; }));
    return {};
  }

  void OnUpdate(const app::AppContext& ctx, F32 dt) override {
    orbit_input_.Apply(*orbit_, ctx.input, ui_, dt);
    // g5 — THE RESIDUAL ADR-0193 LEFT OPEN. It tracked the pivot exactly, which
    // is §17.2.2's LOOK-AT camera and has no lag; the book's follow camera
    // "acts much like a look-at camera focused on the player character … but
    // its motion typically LAGS the player." The ease is on the PIVOT because
    // OrbitComponent::stiffness eases the angles and distance and deliberately
    // not the target (its own comment says why).
    const Vec3 want = player_.position + Vec3{0.0f, 1.2f, 0.0f};
    camera_pivot_ +=
        (want - camera_pivot_) * (1.0f - std::exp(-kCameraStiffness * dt));
    orbit_->pivot = camera_pivot_;
    // Flow at FRAME rate: HandleInput is edge-triggered and would misfire in
    // the 0..N-per-frame step, and ApplyPending must never run mid-Update.
    screens_.HandleInput(ctx);
    screens_.ApplyPending(ctx);
    screens_.Animate(ctx, dt);
  }

  // The whole simulation now belongs to the TOP screen, so a terminal state
  // freezes the world by existing rather than by a `paused_` flag.
  void FixedUpdate(const app::AppContext& ctx, F32 dt) override {
    screens_.UpdateTop(ctx, dt);
  }

  RenderFrame Extract(const app::AppContext& ctx) override {
    RenderFrame frame = BuildSceneFrame(scene_, ctx);
    DrawLevel(frame, level_);
    DrawCover(frame);
    DrawObjective(frame, leg_);
    DrawPlayer(frame, pose_, clips_->Skeleton(), agent_->Skeleton(),
               gaits_, map_, player_);
    DrawGuards(frame);
    return frame;
  }

  // The overlay the first version never had — and the inspector panel it built
  // was never SUBMITTED either, so the "Player (GEA 17.2.1)" section it
  // declares has been invisible since ADR-0193 (event:2026-09-08#54).
  RenderFrame BuildOverlay(const app::AppContext& ctx) override {
    if (!white_) {
      return {};
    }
    const Vec2 cursor = ctx.input.CursorPosition();
    const bool pressed = ctx.input.IsMouseDown(platform::MouseButton::kLeft);
    ui_.BeginFrame(cursor, pressed, ctx.window.FramebufferSize(), font_.get(),
                   white_->Handle());
    screens_.BuildUi(ctx, ui_);
    DrawInspector(inspector_, ctx);
    return ui_.EndFrame();
  }

  // --- what the flow screens drive (§16.10) ----------------------------------
 public:
  [[nodiscard]] bool Autopilot() const { return autopilot_ != 0; }
  [[nodiscard]] bool Finished() const { return outcome_ != Outcome::kRunning; }
  [[nodiscard]] bool Won() const { return outcome_ == Outcome::kCompleted; }

  void StepWorld(const app::AppContext& ctx, F32 dt) {
    StepThePlayer(ctx, dt);
    crowd_.Update(dt);
    // BEFORE `scene_.Update`, which is where `OrbitComponent` places the
    // camera — §13.5.2's rule for game-driven bodies read across to a camera:
    // resolve, then let the driver run.
    StepCameraCollision(orbit_, walls_);
    scene_.Update(dt);
    StepTheGuards(ctx, dt);
    StepTheBodies(dt);
    StepTheAudio();
    StepTheFlow();
    ++steps_;
    ProbeCamera(probe_, scene_.Get(camera_node_), orbit_, walls_);
    Assert();
  }

  // g6 — GEA §13.5.3.7's camera collision, and the third of §17.2.2's three
  // follow-camera parts. The other two were already here: "its motion
  // typically LAGS the player" (the pivot ease above) and "some degree of
  // control over the camera's orientation" (`examples::OrbitInput`). A follow
  // camera "also includes advanced collision detection and avoidance logic",
  // and until 2026-09-10 this one did not.
  //
  // MEASURED BEFORE IT WAS BUILT, and the number moved the goal. The camera's
  // centre is never inside a solid here — 0 of 1400 steps across both routes,
  // so "never clips geometry" would have been a gate that could not fail. What
  // happens instead is §13.5.3.7's FIFTH bullet: route 2 spent 143 CONSECUTIVE
  // steps, 36% of the run, with the wall between the camera and the player, up
  // to 7.88 m past it. The player was simply invisible for 2.4 seconds
  // (`event:2026-09-10#17`).
  //
  // The limit is written every fixed step and read by `OrbitComponent::Update`
  // AFTER its ease, which is what makes the pull-in immediate and the release
  // smooth. The direction comes from the component rather than being recomputed
  // here — resolving along a line the camera is not on would clear one wall and
  // ignore another.

  // The assertion v1's measurement turned into a number, run every step. It is
  // NOT "the camera is not inside geometry" — that was already true and
  // therefore not a gate — but "the camera is not on the far side of geometry
  // from the player", which was false 36% of one route.

  // §16.10: "Often, failure sends the player back to the beginning of the
  // current state, so he or she can try again." One state, one retry — not a
  // checkpoint system, and the guards go back too or the second attempt starts
  // in a world the first one rearranged.
  void ResetRun() {
    player_.position = kStart;
    player_.velocity = Vec3{};
    player_.motion = nav::CharacterMotion{.position = kStart, .grounded = true};
    player_.desired = Vec3{};
    kerb_airborne_ = 0;
    off_ground_ = 0;
    player_.loco = anim::LocomotionState{};
    player_.phase = 0.0f;
    camera_pivot_ = kStart + Vec3{0.0f, 1.2f, 0.0f};  // the follow SNAPS on a
                                                      // cut; lag is for motion
    leg_ = 1;
    exposure_ = 0.0f;
    outcome_ = Outcome::kRunning;
    steps_ = 0;
    auto_leg_ = 0;
    auto_seconds_ = 0.0f;
    auto_hold_ = -1.0f;
    ++retries_;
    closest_guard_ = 99.0f;
    for (Usize g = 0; g < kGuards; ++g) {
      for (const physics::BodyId body : rag_[g].bodies) {
        body_world_.RemoveBody(body);  // and its joints, which go first
      }
      rag_[g] = Limp{};
      guard_.disabled[g] = false;
      guard_.aware[g] = ai::Awareness{};
      guard_.loco[g] = anim::LocomotionState{};
      guard_.phase[g] = 0.0f;
      guard_.reported[g].clear();
      boards_.Agent(g)->ClearAll();
      crowd_.RemoveAgent(guard_.id[g]);
    }
    if (auto spawned = SpawnGuards(); !spawned) {
      LogWarn("infiltration: retry could not respawn the guards: {}",
              spawned.error().message);
    }
    // The hit boxes went with the bodies above, so the respawned guards need
    // them again — a retried run whose guards had no bodies would fail every
    // takedown for a reason that looks like the aim.
    BuildHitBoxes();
    LogInfo("infiltration: RETRY {} — back to the beginning of the state",
            retries_);
  }

  // g1 — THE DETECTION METER, and nothing else on the HUD. §17.2.3's test is
  // whether the player can perceive the characters' motivations; the same test
  // on this side of the screen is whether they can see how exposed they are.
  // Nothing here is new state: it is `ai::UpdateAwareness`'s own confidence,
  // aggregated over the guards that can still see, and DRAWN.
  void DrawHud(ui::Context& ui) const {
    // 46 down, not 22: a 24 px line ABOVE the bar needs room for it, and the
    // first layout put "DETECTION" half off the top of the screen.
    const ui::Rect bar =
        ui.Anchored(ui::Anchor::kTop, 300.0f, 20.0f, Vec2{0.0f, 46.0f});
    ui.Panel(bar, kHudTrack);
    ui.Panel(ui::Rect{.x = bar.x + 2.0f,
                      .y = bar.y + 2.0f,
                      .w = bar.w - 4.0f,
                      .h = bar.h - 4.0f},
             kHudBack);
    const F32 fill = std::clamp(exposure_, 0.0f, 1.0f);
    const Vec4 tint = fill < 0.5f
                          ? Mix(kMeterCalm, kMeterWary, fill * 2.0f)
                          : Mix(kMeterWary, kMeterHot, (fill - 0.5f) * 2.0f);
    ui.Panel(ui::Rect{.x = bar.x + 2.0f,
                      .y = bar.y + 2.0f,
                      .w = (bar.w - 4.0f) * fill,
                      .h = bar.h - 4.0f},
             tint);
    const F32 middle = bar.x + bar.w * 0.5f;
    ui.LabelCentered(middle, bar.y - 28.0f, "DETECTION", kHudText);
    ui.LabelCentered(middle, bar.y + bar.h + 8.0f,
                     leg_ == 1 ? "OBJECTIVE: reach the marker in their half"
                               : "OBJECTIVE: get back to the start",
                     kGoalMark);
    Usize down = 0;
    for (Usize g = 0; g < kGuards; ++g) {
      down += guard_.disabled[g] ? 1 : 0;
    }
    if (down > 0) {
      ui.LabelCentered(middle, bar.y + bar.h + 36.0f,
                       std::format("{} of {} guards down", down, kGuards),
                       kHudText);
    }
  }

 private:
  // The cover points the policy pushes — drawn, so "why did it run there" is
  // answerable by looking.

  // Both spawns in one place, because a RETRY needs them as much as Load does.
  // Detour has no teleport, so a reset removes the agents and adds them back —
  // which also drops their paths and avoidance history, exactly as intended.
  Result<void> SpawnGuards() {
    const nav::AgentParams walker{.radius = 0.35f,
                                  .height = 1.8f,
                                  .max_speed = 2.0f,
                                  .max_acceleration = 8.0f};
    for (Usize g = 0; g < kGuards; ++g) {
      auto id = crowd_.AddAgent(kGuardSpawn[g], walker);
      if (!id) {
        return std::unexpected(id.error());
      }
      guard_.id[g] = *id;
      guard_.target[g] = kGuardSpawn[0];
      (void)crowd_.SetTarget(guard_.id[g], guard_.target[g]);
    }
    return {};
  }

  // §17.2.1's "human interface device systems", plus the autopilot that makes
  // the whole slice checkable with no keyboard and no display.
  [[nodiscard]] Vec3 Intent(const app::AppContext& ctx, F32 dt) {
    if (autopilot_ != 0) {
      return AutopilotIntent(dt);
    }
    const input::InputSnapshot& in = ctx.input;
    // THE STICK IS ANALOGUE AND THE KEYS ARE NOT, which is the whole reason
    // C-G exists for this slice: a keyboard can only ask for full speed, so a
    // player cannot creep up on a guard. The stick's magnitude is kept all the
    // way through rather than normalised away.
    Vec2 axis = in.LeftStick();
    bool analogue = LengthSquared(axis) > 0.0f;
    if (!analogue) {
      axis.y += in.IsDown(platform::Key::kW) ? 1.0f : 0.0f;
      axis.y -= in.IsDown(platform::Key::kS) ? 1.0f : 0.0f;
      axis.x += in.IsDown(platform::Key::kD) ? 1.0f : 0.0f;
      axis.x -= in.IsDown(platform::Key::kA) ? 1.0f : 0.0f;
    }
    if (LengthSquared(axis) < 0.001f) {
      return Vec3{};
    }
    const F32 throttle = analogue ? std::min(Length(axis), 1.0f) : 1.0f;
    axis = Normalize(axis);
    // CAMERA-RELATIVE, which is what makes a third-person control scheme feel
    // like one: forward is where the camera looks, not where the world faces.
    const F32 yaw = orbit_->CurrentYaw();
    const Vec3 forward{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const Vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    // The right trigger runs, the way Space does — and on a pad the throttle
    // already gives everything between a creep and a walk, so the trigger only
    // has to reach the top of the range.
    const bool running =
        in.IsDown(platform::Key::kSpace) ||
        in.Trigger(platform::GamepadAxis::kRightTrigger) > 0.5f;
    const F32 speed = running ? player_.tune.run_speed : player_.tune.walk_speed;
    return Normalize(forward * axis.y + right * axis.x) * speed * throttle;
  }

  // TWO SCRIPTED ROUTES, and the second one is the point (g6). A loop whose
  // fail state has never executed is the same shape as a gate that cannot
  // fail, and this repository has recorded that shape nine times — so route 1
  // wins and route 2 loses, on purpose.
  //
  // ARRIVAL-DRIVEN with a time budget as the fallback, rather than the pure
  // wall-clock schedule the first version used: a leg that cannot be reached
  // then costs its own budget instead of stalling the whole run.
  [[nodiscard]] Vec3 AutopilotIntent(F32 dt) {
    auto_seconds_ += dt;
    if (autopilot_ >= 2) {
      return BlownIntent();
    }
    // Leg 0 — STALK. Steering at the guard ITSELF approaches it from behind by
    // construction: the player walks at 2.2 m/s and a guard tops out at 2.0,
    // so a chase closes and the angle gate is satisfied without scripting an
    // approach vector. The takedown then fires from `WantsTakedown`.
    if (auto_leg_ == 0) {
      if (!guard_.disabled[0]) {
        if (auto_seconds_ < kStalkBudget) {
          const Vec3 to = crowd_.AgentPosition(guard_.id[0]) - player_.position;
          return LengthSquared(to) < 0.01f ? Vec3{}
                                           : Normalize(to) * player_.tune.walk_speed;
        }
      } else {
        // ...and then LOITER SQUARE IN FRONT OF IT. Not decoration: the g4
        // assertion needs a full second of the player standing inside where
        // that guard's cone would be with its confidence pinned at zero, which
        // is what separates "disabled" reaching perception from a guard merely
        // drawn grey while `UpdateAwareness` still runs on it.
        if (auto_hold_ < 0.0f) {
          auto_hold_ = auto_seconds_ + kLoiterSeconds;
        }
        if (auto_seconds_ < auto_hold_) {
          const Vec3 at = crowd_.AgentPosition(guard_.id[0]);
          const Vec3 forward{std::sin(guard_.loco[0].facing), 0.0f,
                             std::cos(guard_.loco[0].facing)};
          const Vec3 to = at + forward * 1.3f - player_.position;
          return LengthSquared(to) < 0.02f ? Vec3{}
                                           : Normalize(to) * player_.tune.walk_speed;
        }
      }
      auto_leg_ = 1;
      auto_seconds_ = 0.0f;
    }
    static constexpr std::array<Vec3, 6> kRoute{{
        Vec3{7.0f, 0.0f, -6.0f},  // along the -z side, behind the wall
        Vec3{7.0f, 0.0f, 5.0f},   // round the open end into the guards' half
        kObjective,               // THE OBJECTIVE, in view of guard 1
        Vec3{7.0f, 0.0f, 5.0f},   // back toward the gap
        Vec3{7.0f, 0.0f, -6.0f},  // through it
        kExtraction,              // and out the way we came in
    }};
    const Usize w = auto_leg_ - 1;
    if (w >= kRoute.size()) {
      return Vec3{};
    }
    const Vec3 to = kRoute[w] - player_.position;
    // 0.8 m, INSIDE the objective's own 1.2 m radius, so arriving at the
    // objective waypoint always registers the objective first.
    if (LengthSquared(to) < 0.64f || auto_seconds_ > kLegBudget) {
      ++auto_leg_;
      auto_seconds_ = 0.0f;
    }
    return LengthSquared(to) < 0.01f ? Vec3{} : Normalize(to) * player_.tune.run_speed;
  }

  // Route 2 — GET IN A GUARD'S FACE. It steers at the point 2.2 m in FRONT of
  // the nearest live guard, and that is the entire difference between the two
  // routes: walking up behind one is a takedown, walking round to the front of
  // one is a failed run. The same dot product decides both.
  [[nodiscard]] Vec3 BlownIntent() const {
    for (Usize g = 0; g < kGuards; ++g) {
      if (guard_.disabled[g]) {
        continue;
      }
      const Vec3 at = crowd_.AgentPosition(guard_.id[g]);
      const Vec3 forward{std::sin(guard_.loco[g].facing), 0.0f,
                         std::cos(guard_.loco[g].facing)};
      const Vec3 to = at + forward * 2.2f - player_.position;
      return LengthSquared(to) < 0.04f ? Vec3{}
                                       : Normalize(to) * player_.tune.walk_speed;
    }
    return Vec3{};
  }

  // g4's input. The autopilot HOLDS the key on route 1 and never on route 2 —
  // range and angle are what gate it, so the routes differ in where they walk
  // rather than in what they press.
  [[nodiscard]] bool WantsTakedown(const app::AppContext& ctx) const {
    if (autopilot_ == 1) {
      return true;
    }
    // ROUTE 2 PRESSES IT TOO, and only from the FRONT — which is the negative
    // half of h3 and the reason the route exists at all. Held down the whole
    // way round it would eventually walk behind a guard and earn a legitimate
    // takedown, so this fires exactly where the answer must be no.
    if (autopilot_ == 2) {
      return FacingAGuardInReach();
    }
    return ctx.input.IsDown(platform::Key::kE) ||
           ctx.input.IsGamepadDown(platform::GamepadButton::kSouth);
  }

  // A live guard inside the reach, being looked at from IN FRONT of it.
  // Every character's movement capsule, republished each step. ONE LIST SERVES
  // EVERY MOVER, with the self-exclusion on the query object rather than in the
  // list, because a per-mover filtered copy is an allocation per character per
  // step — and because a character's own capsule at t = 0 shadows every real
  // surface, so the exclusion is load-bearing rather than tidy
  // (`event:2026-09-10#29`).
  //
  // A DOWNED GUARD IS NOT A BLOCKER. It is limp on the floor and its movement
  // capsule stands 1.8 m tall wherever the crowd agent stopped; leaving it in
  // would put an invisible pillar over a body you are meant to walk past.
  void PublishCharacterCapsules() {
    capsules_.clear();
    capsules_.push_back(nav::CharacterCapsule{.feet = player_.motion.position,
                                              .radius = player_.body.radius,
                                              .height = player_.body.height,
                                              .layer = kCharacterLayer,
                                              .id = kPlayerCapsuleId});
    for (Usize g = 0; g < kGuards; ++g) {
      if (guard_.disabled[g]) {
        continue;
      }
      capsules_.push_back(
          nav::CharacterCapsule{.feet = crowd_.AgentPosition(guard_.id[g]),
                                .radius = kGuardRadius,
                                .height = player_.body.height,
                                .layer = kCharacterLayer,
                                .id = kGuardCapsuleId + g});
    }
  }

  [[nodiscard]] bool FacingAGuardInReach() const {
    for (Usize g = 0; g < kGuards; ++g) {
      if (guard_.disabled[g]) {
        continue;
      }
      const Vec3 at = crowd_.AgentPosition(guard_.id[g]);
      const Vec3 to = player_.position - at;
      const F32 range = Length(to);
      const Vec3 forward{std::sin(guard_.loco[g].facing), 0.0f,
                         std::cos(guard_.loco[g].facing)};
      if (range > 1e-4f && range <= kTakedownRange &&
          Dot(forward, to / range) > kTakedownBehind) {
        return true;
      }
    }
    return false;
  }

  void StepThePlayer(const app::AppContext& ctx, F32 dt) {
    // The two halves that need the APP stay here, in the order they ran:
    // intent comes from input or the autopilot, and the capsule list is
    // published from the live guards before anything is swept against it.
    const Vec3 intent = Intent(ctx, dt);
    PublishCharacterCapsules();
    StepPlayer(player_, intent, walls_, capsules_, kPlayerCapsuleId,
                        kHalf, LocoConfig(), dt);
  }

  // g4 — THE TAKEDOWN, and what it deliberately leaves undone.
  //
  // A disabled guard stops perceiving, stops deciding and stops moving. IT DOES
  // NOT GO LIMP: it holds its last pose, standing bolt upright, which looks
  // exactly as wrong as it sounds. That is the finding this task exists to
  // produce — §12.9's stage 5 has said "awaiting a consumer" since Phase E and
  // §13.4 names its precondition, *"a rag doll (specialized constraints that
  // mimic the behavior of various human or animal joints)"*. The trigger has a
  // named consumer now; the ragdoll is the next plan's, and it opens on this
  // rather than on appetite.
  // h3 — IT IS AIMED NOW, and that is the difference. Until 2026-09-10 this was
  // a distance and a dot product: nothing was aimed at, nothing could be
  // missed, and no limb existed to hit. §13.5.3.2's opening is the cheapest
  // honest consumer of the hit boxes — "Sometimes projectiles are implemented
  // using raycasts. On the frame that the weapon is fired, we shoot off a
  // raycast, determine what object was hit and immediately impart the impact."
  // The section's three complaints about that (no travel time, no gravity drop,
  // a third-person reticle that does not agree with the muzzle) are all about
  // distance, and none of them reaches 1.7 m.
  //
  // The ray answers with a BONE, because `user_id` carries one — so a takedown
  // now says which limb it landed on, and a takedown that lands on the level
  // instead is refused.
  [[nodiscard]] bool TryTakedown(Usize g, Vec3 at, Vec3 forward) {
    const Vec3 to = player_.position - at;
    const F32 range = Length(to);
    if (range > kTakedownRange || range < 1e-4f) {
      return false;
    }
    if (Dot(forward, to / range) > kTakedownBehind) {
      return false;  // it is facing you: that is a fight, not a takedown
    }
    // The reach, from the player's chest along the direction the player faces.
    // A guard within `kTakedownRange` whose hit boxes the player is NOT looking
    // at is not taken down — the range check above is necessary and no longer
    // sufficient.
    const Vec3 chest = player_.position + Vec3{0.0f, 1.2f, 0.0f};
    const Vec3 aim{std::sin(player_.loco.facing), 0.0f,
                   std::cos(player_.loco.facing)};
    const std::optional<GeometryHit3> hit = body_world_.Raycast(
        Ray3{.origin = chest, .direction = aim}, kBodyLayer);
    if (!hit || hit->t > kTakedownRange || !IsHitBox(hit->id) ||
        HitBoxGuard(hit->id) != g) {
      return false;
    }
    last_hit_bone_ = BoneName(HitBoxBone(hit->id));
    last_hit_range_ = hit->t;
    // The POST-CONDITION, recorded here and checked in `Assert` — which is the
    // point of recording it rather than re-deriving it there. Deleting the
    // refusal above stops changing the refusal COUNT (the ray refuses some
    // front-on attempts by itself, which is how that gate survived a mutation
    // on 2026-09-10) and starts landing takedowns with this number positive.
    takedown_dot_ = Dot(forward, to / range);
    guard_.disabled[g] = true;
    (void)crowd_.SetAgentMaxSpeed(guard_.id[g], 0.01f);
    (void)crowd_.SetTarget(guard_.id[g], at);
    guard_.aware[g] = ai::Awareness{};  // and it perceives nothing, ever again
    boards_.Agent(g)->Set(StringId{"state"}, std::string("down"));
    boards_.Agent(g)->Set(StringId{"posture"}, 0.0);
    guard_.reported[g] = "down";
    GoLimp(g);
    LogInfo(
        "infiltration: the reach landed on guard {}'s {} at {:.2f} m — GEA "
        "13.5.3.6's hit boxes, hit (h3)",
        g, last_hit_bone_, last_hit_range_);
    LogInfo(
        "infiltration: guard {} is DOWN at {:.1f} m from behind — and it GOES "
        "LIMP, which it did not until 2026-09-08: ADR-0194 recorded the "
        "upright body as GEA 12.9 stage 5's trigger firing with a consumer, "
        "ADR-0195 built 13.4's constraints, and ADR-0196 is the ragdoll.",
        g, range);
    return true;
  }

  void StepTheGuards(const app::AppContext& ctx, F32 dt) {
    const bool takedown = WantsTakedown(ctx);
    for (Usize g = 0; g < kGuards; ++g) {
      const Vec3 at = crowd_.AgentPosition(guard_.id[g]);
      // A downed guard is skipped WHOLE — no gait, no perception, no policy
      // call — so its pose, its confidence and its blackboard all freeze.
      if (guard_.disabled[g]) {
        continue;
      }
      StepGuardGait(guard_, g, crowd_.AgentVelocity(guard_.id[g]),
                    LocoConfig(), dt);

      // PERCEPTION, OF THE PLAYER (ADR-0189). This is what `nav_lab` could not
      // do: it watches a FIXED point, because two mirror-symmetric agents are
      // always wall-separated and never face each other. A player is the target
      // that compromise was standing in for.
      const Vec3 eye = at + Vec3{0.0f, 1.6f, 0.0f};
      const Vec3 chest = player_.position + Vec3{0.0f, 1.2f, 0.0f};
      const Vec3 forward{std::sin(guard_.loco[g].facing), 0.0f,
                         std::cos(guard_.loco[g].facing)};
      if (takedown) {
        if (TryTakedown(g, at, forward)) {
          continue;
        }
        // A REFUSAL IN REACH IS THE EVIDENCE, and it has to be counted where it
        // happens: "the run did not end in a takedown" is also what a broken
        // button, an unspawned guard and a missing hit box all look like.
        if (Length(player_.position - at) <= kTakedownRange) {
          ++takedown_refused_;
        }
      }
      StepGuardAwareness(guard_, g, eye, forward, chest, walls_, cone_, dt);

      ai::Blackboard* board = boards_.Agent(g);
      board->Set(StringId{"at_x"}, static_cast<F64>(at.x));
      board->Set(StringId{"at_z"}, static_cast<F64>(at.z));
      board->Set(StringId{"confidence"},
                 static_cast<F64>(guard_.aware[g].confidence));
      board->Set(StringId{"remembered"}, guard_.aware[g].seconds_since_seen >= 0.0f);
      board->Set(StringId{"last_x"},
                 static_cast<F64>(guard_.aware[g].last_known_position.x));
      board->Set(StringId{"last_z"},
                 static_cast<F64>(guard_.aware[g].last_known_position.z));
      board->Set(StringId{"target_x"}, static_cast<F64>(chest.x));
      board->Set(StringId{"target_z"}, static_cast<F64>(chest.z));
      board->Set(StringId{"arrived"}, Length(at - guard_.target[g]) < 1.0f);

      const script::Value who{static_cast<F64>(g)};
      if (auto called = policy_->Call("agent_policy", std::span{&who, 1});
          !called) {
        LogInfo("infiltration: policy failed for guard {}: {}", g,
                called.error().message);
        return;
      }
      const Vec3 want{static_cast<F32>(board->GetNumber(StringId{"want_x"})),
                      0.0f,
                      static_cast<F32>(board->GetNumber(StringId{"want_z"}))};
      if (Length(want - guard_.target[g]) > 0.5f) {
        guard_.target[g] = want;
        (void)crowd_.SetTarget(guard_.id[g], want);
      }
      const std::string_view now = board->GetText(StringId{"state"}, "?");
      if (now != guard_.reported[g]) {
        guard_.reported[g] = std::string{now};
        LogInfo("infiltration: guard {} -> {} (confidence {:.2f})", g, now,
                guard_.aware[g].confidence);
      }
    }
  }

  // --- r4/r5: THE GUARD GOES LIMP (GEA §13.4.8.7, §13.5.3.8, §16.6.3) -------
  //
  // ADR-0194 built the takedown and deliberately left the body standing bolt
  // upright, so §12.9's stage 5 would open on a trigger rather than on
  // appetite. This is the gate being served.
  //
  // THE BODIES ARE BUILT AT THE MOMENT OF THE TAKEDOWN, from the pose the guard
  // is standing in — which is §13.5.3.8's own advice read the only way this
  // engine can: "it's actually quite common for characters to have entirely
  // different collision/physics representations depending on whether they're
  // conscious or unconscious." A conscious guard here has no bodies at all, so
  // there is no first representation to transition FROM, and nothing to
  // interpenetrate at the instant of the switch.
  // h2 — §13.5.3.6's representation, built once per guard and from the mapping
  // that already exists. `anim::RagdollRig` is the joint->capsule table
  // ADR-0196 authored, held as DATA with no physics type in it precisely so a
  // second consumer could use it; this is that consumer, and it invents no
  // second table.
  //
  // THE RADII ARE THE RIG'S OWN, not wider ones, and that is the decision the
  // switch depends on. §13.5.3.8 warns that overlapping bodies handed to the
  // solver get impulses "that would tend to make the limbs explode outward" —
  // so hit boxes inflated for silhouette coverage could not BECOME the ragdoll,
  // and coverage is what this trades away for one representation instead of
  // two.
  void BuildHitBoxes() {
    if (rag_rig_.BoneCount() == 0) {
      return;
    }
    const std::span<const anim::RagdollBone> bones = rag_rig_.Bones();
    for (Usize g = 0; g < kGuards; ++g) {
      rag_[g].bodies.assign(bones.size(), physics::BodyId{});
      PoseRagdollTargets(pose_, rag_rig_, clips_->Skeleton(),
                       agent_->Skeleton(), gaits_, map_,
                       guard_.loco[g], guard_.phase[g], LiveToWorld(g),
                       rag_targets_);
      for (Usize b = 0; b < bones.size(); ++b) {
        const auto id = body_world_.AddBody(
            {.transform = rag_targets_[b],
             .shape = physics::Shape::Capsule(bones[b].radius,
                                              bones[b].length * 0.5f),
             // Ignored while game-driven — §13.5.1.2's "infinitely massive" —
             // and REMEMBERED for the moment it stops being, which is what
             // keeps the ragdoll weighing what its rig authored.
             .mass = bones[b].mass,
             .motion = physics::MotionType::kGameDriven,
             .friction = 0.6f,
             .layer = kBodyLayer,
             .user_id = HitBoxId(g, b)});
        if (!id) {
          LogWarn("infiltration: no hit box for guard {} bone {}: {}", g, b,
                  id.error().message);
          rag_[g].bodies.clear();
          break;
        }
        rag_[g].bodies[b] = *id;
      }
    }
    LogInfo(
        "infiltration: {} hit boxes per guard, game-driven from the animated "
        "pose (GEA 13.5.3.6) — and they are the SAME bodies the ragdoll uses",
        bones.size());
  }

  // Where the animated pose says a live guard's hit boxes should be, into
  // `rag_targets_`. This is §16.6.3's `ApplySkeletonsToRagDolls` unchanged —
  // the same call the limp branch makes — and the only difference is the frame:
  // the LIVE agent's, because a conscious guard is still walking, rather than
  // the one frozen at the takedown.

  // The same placement `LimpToWorld` does, from the agent rather than from the
  // pose frozen at the takedown.
  [[nodiscard]] Mat4 LiveToWorld(Usize g) const {
    return MakeTranslation(crowd_.AgentPosition(guard_.id[g])) *
           QuatToMat4(QuatFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f},
                                        guard_.loco[g].facing));
  }

  void GoLimp(Usize g) {
    if (rag_rig_.BoneCount() == 0 || rag_[g].live || rag_[g].bodies.empty()) {
      return;
    }
    // The frame the whole ragdoll lives in, FROZEN here: the crowd agent stops
    // moving but the simulation carries the body wherever it falls, and the
    // difference has to land in the joints' local poses rather than in a
    // placement that keeps chasing an agent.
    rag_[g].at = crowd_.AgentPosition(guard_.id[g]);
    rag_[g].facing = guard_.loco[g].facing;
    const Mat4 to_world = LimpToWorld(rag_[g]);

    // The animated pose, composed — §16.6.3's "the animation system produces an
    // intermediate, local-space skeletal pose".
    PoseFor(pose_, clips_->Skeleton(), agent_->Skeleton(), gaits_, map_,
            guard_.loco[g], guard_.phase[g]);
    pose_.globals.resize(agent_->Skeleton().JointCount());
    anim::ComposeGlobals(agent_->Skeleton(), pose_.local, pose_.globals);

    rag_targets_.resize(rag_rig_.BoneCount());
    anim::PoseToRagdollTargets(rag_rig_, pose_.globals, to_world, rag_targets_);

    const std::span<const anim::RagdollBone> bones = rag_rig_.Bones();
    // NOTHING IS BUILT HERE ANY MORE — the bodies have existed since the guard
    // spawned, as game-driven hit boxes, and this is §13.5.1.4's one-line
    // transition: "as soon as the character drops or throws the object, it
    // would be changed to physics-driven so the dynamics simulation can take
    // over its motion ... easily accomplished in Havok by simply changing the
    // motion type at the moment of release."
    //
    // AND THEY ARE ALREADY MOVING, which is the part a rebuild could not give.
    // `MoveBody` left each body with the velocity that carried it to this
    // pose, so a guard walking at 1.4 m/s goes limp AT 1.4 m/s and keeps
    // going; bodies constructed at this instant would start from a dead stop
    // (`event:2026-09-10#3`).
    for (Usize b = 0; b < bones.size(); ++b) {
      if (!body_world_.SetMotionType(rag_[g].bodies[b],
                                     physics::MotionType::kDynamic)) {
        LogWarn("infiltration: limb {} would not go dynamic", b);
        return;
      }
    }
    rag_[g].joints.clear();
    for (Usize b = 0; b < bones.size(); ++b) {
      const Usize parent = bones[b].parent_bone;
      if (parent == anim::RagdollBone::kNoBone) {
        continue;  // the pelvis is free, which is what makes the body fall
      }
      // The joint sits at the SKELETON joint, not at the capsule's centre —
      // the capsule is offset half a bone down, and hanging a shoulder off the
      // middle of the upper arm is a visibly wrong elbow.
      const Vec3 anchor =
          TransformPoint(to_world * pose_.globals[bones[b].joint], Vec3{});
      // §13.4.8.7's "specialized constraints". The twist axis runs DOWN the
      // bone, which is the +Y convention `anim/ragdoll.hpp` documents and
      // `ragdoll_test` pins.
      const Quat& rot = rag_targets_[b].rotation;
      physics::ConstraintDesc desc = physics::ConstraintDesc::SwingTwist(
          anchor, Rotate(rot, Vec3{0.0f, 1.0f, 0.0f}),
          Rotate(rot, Vec3{1.0f, 0.0f, 0.0f}), bones[b].cone_half_angle,
          bones[b].twist_min, bones[b].twist_max);
      // §13.4.8.8. Powered at full authority for the first moments so the body
      // HOLDS the pose it was standing in, then faded — see StepTheBodies.
      desc.motor = physics::ConstraintMotor{.enabled = true,
                                            .max_torque = kLimpTorque,
                                            .frequency = 12.0f,
                                            .damping = 1.0f};
      const auto joint = body_world_.AddConstraint(rag_[g].bodies[parent],
                                                   rag_[g].bodies[b], desc);
      if (!joint) {
        LogWarn("infiltration: no limb joint: {}", joint.error().message);
        continue;
      }
      rag_[g].joints.push_back(*joint);
    }
    rag_[g].live = true;
    rag_[g].power = 1.0f;
    rag_[g].limp_from = body_world_.BodyTransform(rag_[g].bodies[0]).position;
    rag_[g].limp_step = steps_;
    LogInfo(
        "infiltration: guard {} is LIMP — the SAME {} hit boxes it has carried "
        "since it spawned, now physics-driven, plus {} joints, {} kg (GEA 12.9 "
        "stage 5 served, 13.5.1.4's motion type switched)",
        g, rag_[g].bodies.size(), rag_[g].joints.size(), RigMass());
  }

  // The authoring name of a rig bone's joint, which is what makes a hit
  // report readable rather than an index. `debug_name` is the skeleton's own
  // "kept for diagnostics and tooling" field, and this is diagnostics.
  [[nodiscard]] std::string BoneName(Usize bone) const {
    const std::span<const anim::RagdollBone> bones = rag_rig_.Bones();
    if (bone >= bones.size()) {
      return "?";
    }
    const std::span<const resources::Joint> joints =
        agent_->Skeleton().Joints();
    const Usize joint = bones[bone].joint;
    return joint < joints.size() ? joints[joint].debug_name : "?";
  }


  [[nodiscard]] F32 RigMass() const {
    F32 total = 0.0f;
    for (const anim::RagdollBone& bone : rag_rig_.Bones()) {
      total += bone.mass;
    }
    return total;
  }

  // §16.6.3's loop, in its order, inside ONE FixedUpdate:
  //
  //   pose  ->  ApplySkeletonsToRagDolls  ->  Simulate  ->
  //   ApplyRagDollsToSkeletons
  //
  // "So once again, the updating of the animation and physics systems must
  // occur in a particular order in order to produce correct results."
  void StepTheBodies(F32 dt) {
    if (rag_rig_.BoneCount() == 0) {
      return;
    }
    for (Usize g = 0; g < kGuards; ++g) {
      if (rag_[g].bodies.empty()) {
        continue;
      }
      // 0. A LIVE GUARD'S BODIES ARE GAME-DRIVEN, and §13.5.2 puts them here:
      // "Update game-driven rigid bodies. The transforms of all game-driven
      // rigid bodies in the physics world are updated so that they match the
      // transforms of their counterparts (game objects or JOINTS) in the game
      // world" — before the step, never after.
      if (!rag_[g].live) {
        PoseRagdollTargets(pose_, rag_rig_, clips_->Skeleton(),
                           agent_->Skeleton(), gaits_, map_,
                           guard_.loco[g], guard_.phase[g],
                           LiveToWorld(g), rag_targets_);
        HoldOnTargets(rag_[g], body_world_, rag_targets_, dt);
        continue;
      }
      // 1. the animated pose the motors chase. It is FROZEN — a downed guard's
      // gait stopped advancing at the takedown — so this is the pose it died
      // in, which is exactly what §13.4.8.8 wants a rest angle to be.
      PoseFor(pose_, clips_->Skeleton(), agent_->Skeleton(), gaits_, map_,
              guard_.loco[g], guard_.phase[g]);
      anim::ComposeGlobals(agent_->Skeleton(), pose_.local, pose_.globals);
      anim::PoseToRagdollTargets(rag_rig_, pose_.globals, LimpToWorld(rag_[g]),
                                 rag_targets_);
      // 2. drive the constraints toward it, at whatever authority is left.
      DriveLimp(rag_[g], body_world_, rag_rig_, rag_targets_, dt);
    }
    // UNCONDITIONALLY NOW, where it used to run only while something was limp:
    // a game-driven body does not move until the world steps, so hit boxes that
    // track a walking guard need every step.
    body_world_.Step(dt);

    // h2's ONE MEASUREMENT: did the bodies arrive where they were sent? This is
    // the assertion the representation lives or dies by, and it is deliberately
    // NOT "the hit boxes stayed out of the wall" — §13.5.3.6 says they will
    // not: "Because these bodies are game-driven, they won't avoid
    // interpenetrations with immovable objects in the physics world." Measured
    // only until it has fired, so it costs nothing for the rest of the run.
    if (!asserted_tracking_) {
      for (Usize g = 0; g < kGuards; ++g) {
        if (rag_[g].live || rag_[g].bodies.empty()) {
          continue;
        }
        PoseRagdollTargets(pose_, rag_rig_, clips_->Skeleton(),
                           agent_->Skeleton(), gaits_, map_,
                           guard_.loco[g], guard_.phase[g],
                           LiveToWorld(g), rag_targets_);  // the same pose: nothing advanced it since
        for (Usize b = 0; b < rag_[g].bodies.size(); ++b) {
          const Transform now = body_world_.BodyTransform(rag_[g].bodies[b]);
          hit_box_drift_ = std::max(
              hit_box_drift_, Length(now.position - rag_targets_[b].position));
          ++hit_box_samples_;
        }
      }
    }
  }

  // The other direction, called from the draw path because that is where the
  // pose is wanted — the bodies were already stepped in FixedUpdate.

  // ADR-0191's muffling, applied to the player's own sound. The listener is the
  // camera; the source is the player; the wall is between them or it is not.
  void StepTheAudio() {
    if (audio_ == nullptr) {
      return;
    }
    // The listener is the PLAYER, not the camera: an orbit camera can swing
    // behind a wall while the player stands in the open, and the mix should
    // follow the character rather than the framing.
    const audio::Listener listener{.position =
                                       player_.position + Vec3{0.0f, 1.6f, 0.0f}};
    audio_->SetListener(listener);
    // The regions are the two halves of the corridor, which is what makes the
    // indirect path answerable without tracing it (§14.4.5.3's rule of thumb).
    const auto region = [](F32 z) {
      return audio::AcousticRegion{z >= 0.0f ? 1U : 2U};
    };
    for (Usize g = 0; g < kGuards; ++g) {
      if (!voices_[g].Valid()) {
        continue;
      }
      if (guard_.disabled[g]) {
        audio_->SetSpatialGain(voices_[g], 0.0f);  // a downed guard is silent
        continue;
      }
      const Vec3 at = crowd_.AgentPosition(guard_.id[g]) + Vec3{0.0f, 1.2f, 0.0f};
      audio_->SetSpatialPosition(voices_[g], at);
      heard_[g] =
          audio::Propagate(listener, at, walls_, region(listener.position.z),
                           region(at.z), {}, heard_[g], 1.0f / 60.0f);
      audio_->SetSpatialGain(voices_[g], heard_[g].dry_gain);
      audio_->SetWetSend(voices_[g], heard_[g].wet_send);
      audio_->SetFilter(voices_[g], heard_[g].dry_cutoff_hz,
                        heard_[g].wet_cutoff_hz);
    }
  }

  // g1/g2 — the meter, the objective and the catch. All three are aggregation
  // over state that already exists; nothing new is measured.
  void StepTheFlow() {
    exposure_ = 0.0f;
    for (Usize g = 0; g < kGuards; ++g) {
      if (!guard_.disabled[g]) {
        exposure_ = std::max(exposure_, guard_.aware[g].confidence);
      }
    }
    if (outcome_ != Outcome::kRunning) {
      return;
    }
    // SEEN IS NOT CAUGHT — see kCatchRange. A guard has to be certain AND on
    // top of you, which is what leaves the meter something to mean.
    for (Usize g = 0; g < kGuards; ++g) {
      if (guard_.disabled[g] || !guard_.aware[g].visible_now ||
          guard_.aware[g].confidence < kCatchConfidence) {
        continue;
      }
      if (Length(crowd_.AgentPosition(guard_.id[g]) - player_.position) < kCatchRange) {
        outcome_ = Outcome::kCaught;
        LogInfo(
            "infiltration: SPOTTED by guard {} at {:.1f} m after {} steps — "
            "the run is over",
            g, Length(crowd_.AgentPosition(guard_.id[g]) - player_.position), steps_);
        return;
      }
    }
    // TWO LEGS ON PURPOSE: the second is walked under pressure, with the meter
    // already warm and a guard already looking for you.
    const Vec3 goal = leg_ == 1 ? kObjective : kExtraction;
    if (Length(player_.position - goal) >= kArriveRadius) {
      return;
    }
    if (leg_ == 1) {
      leg_ = 2;
      LogInfo("infiltration: objective reached — now get back to the start");
    } else {
      outcome_ = Outcome::kCompleted;
      LogInfo("infiltration: EXTRACTED after {} steps", steps_);
    }
  }

  // §5's assertions, logged once each. Every one is a number or a state name,
  // because a slice driven by a human is unverifiable and four demos in this
  // repo could not demonstrate their own subject.
  void Assert() {
    const auto say = [](const char* what) {
      LogInfo("infiltration: ASSERT {}", what);
    };
    // EITHER GUARD, not guard 0. The first version asserted on guard 0, which
    // starts on the PLAYER'S OWN side of the wall and is therefore never
    // occluded early — an assertion that could not fire for a reason that had
    // nothing to do with the code under test.
    // AND THE CUTOFF HAS TO BE LOW ENOUGH TO MEAN SOMETHING. `!CutoffIsOpen`
    // passed at 18710 Hz — the FIRST STEP of the log-space blend down from
    // open, inaudibly high and settling nowhere near it. That is the same trap
    // audio_probe's transition log fell into two commits ago (the `[blending]`
    // line versus the `SETTLED` one), walked into again here. A threshold in
    // the audible band is what makes the number evidence.
    constexpr F32 kAudiblyMuffled = 6000.0f;
    for (Usize g = 0; g < kGuards && !asserted_muffled_; ++g) {
      if (!audio::CutoffIsOpen(heard_[g].dry_cutoff_hz) &&
          heard_[g].dry_cutoff_hz < kAudiblyMuffled &&
          heard_[g].state != audio::PathState::kFree) {
        asserted_muffled_ = true;
        say(std::format(
                "guard {} is HEARD MUFFLED through the wall — {} at {:.0f} Hz",
                g, StateName(heard_[g].state), heard_[g].dry_cutoff_hz)
                .c_str());
      }
    }
    if (!asserted_seen_ && (guard_.aware[0].visible_now || guard_.aware[1].visible_now)) {
      asserted_seen_ = true;
      say("a guard ACQUIRED the player");
    }
    if (!asserted_posture_) {
      for (Usize g = 0; g < kGuards; ++g) {
        if (boards_.Agent(g)->GetNumber(StringId{"posture"}, 0.0) > 0.0) {
          asserted_posture_ = true;
          say(std::format("guard {} posture {:.2f} — the gesture layer is live",
                          g,
                          boards_.Agent(g)->GetNumber(StringId{"posture"}, 0.0))
                  .c_str());
        }
      }
    }
    // ...and this one waits for the muffled case first, so the PAIR is what is
    // asserted rather than two independent facts. Reporting "clear" before
    // "muffled" is what the first run did, and it proved nothing about the
    // wall.
    if (!asserted_open_ && asserted_muffled_) {
      for (Usize g = 0; g < kGuards; ++g) {
        if (audio::CutoffIsOpen(heard_[g].dry_cutoff_hz) &&
            heard_[g].state == audio::PathState::kFree) {
          asserted_open_ = true;
          say(std::format("guard {} is HEARD CLEARLY — the cutoff is open "
                          "again, so the wall is what changed it",
                          g)
                  .c_str());
          break;
        }
      }
    }
    // ANY guard, and not a DOWNED one. This read `guard_.aware[0]` until g4 arrived
    // and made it unreachable on route 1: a disabled guard's awareness is
    // zeroed forever, so the assertion sat waiting on a memory that could no
    // longer exist. The same class of mistake as the three ADR-0193 records —
    // an assertion that cannot fire for a reason unrelated to its subject.
    for (Usize g = 0; g < kGuards && !asserted_lost_ && asserted_seen_; ++g) {
      if (!guard_.disabled[g] && !guard_.aware[g].visible_now &&
          guard_.aware[g].seconds_since_seen > 0.5f) {
        asserted_lost_ = true;
        say(std::format("guard {} LOST the player and remembers ({:.1f}s ago)",
                        g, guard_.aware[g].seconds_since_seen)
                .c_str());
      }
    }
    // --- the loop's own five (g1–g6) ----------------------------------------
    if (!asserted_meter_ && exposure_ > 0.2f) {
      asserted_meter_ = true;
      say(std::format("the DETECTION METER reads {:.2f} — drawn, so being "
                      "exposed is visible rather than inferred",
                      exposure_)
              .c_str());
    }
    if (!asserted_leg_ && leg_ == 2) {
      asserted_leg_ = true;
      say("the OBJECTIVE's first leg is done — the return is leg two");
    }
    // --- the ragdoll (r4-r6) -------------------------------------------------
    // NUMBERS FIRST, because they run unattended — and then a person looks,
    // which is r7 and not a formality: a ragdoll is the most visual thing in
    // this repository and these five checks would all pass on a body that
    // looked like a bag of sticks.
    // BOTH conditions in the guard, and the first version had only the first:
    // once the body had fallen the loop stopped running and the explosion check
    // could never fire. A check that cannot fire, written in the session that
    // recorded three of them.
    for (Usize g = 0; g < kGuards && (!asserted_fell_ || !asserted_intact_);
         ++g) {
      if (!rag_[g].live || rag_[g].bodies.empty()) {
        continue;
      }
      // THE PELVIS, not the lowest limb, and the mutation that found this is
      // worth keeping written down: with `SetMotionType` deleted from `GoLimp`
      // the bodies never went dynamic and never fell, and this assertion still
      // reported "the body FELL - lowest limb 0.24 m". The rig has no feet
      // (`HumanRagdoll` says so), so the lowest capsule's CENTRE stands at
      // 0.24 m and a 0.25 m threshold was satisfied by a guard on its feet.
      // Vacuous since ADR-0196 landed. The comment below always said pelvis.
      const Vec3 pelvis = body_world_.BodyTransform(rag_[g].bodies[0]).position;
      F32 span = 0.0f;
      bool finite = true;
      for (const physics::BodyId a : rag_[g].bodies) {
        const Vec3 at = body_world_.BodyTransform(a).position;
        finite = finite && std::isfinite(at.x) && std::isfinite(at.y);
        for (const physics::BodyId b : rag_[g].bodies) {
          span = std::max(span,
                          Length(at - body_world_.BodyTransform(b).position));
        }
      }
      // §13.5.3.8's FAILURE MODE, checked rather than hoped: bodies that start
      // interpenetrating make "the collision resolution system ... impart large
      // impulses that would tend to make the limbs explode outward". A human is
      // under 2 m end to end, so a span past 3 m is an explosion and not a
      // sprawl. Measured resting span: about 1.2 m.
      // THE VERDICT COMES FIRST NOW. This used to say "the ragdoll is INTACT"
      // and then add "...and that span is WRONG" underneath, so a run that
      // blew apart reported both — and the same mutation above produced
      // exactly that: "INTACT — 12.41 m across, no explosion". A gate whose
      // headline is green while its own number is red is not a gate.
      if (!asserted_intact_ && steps_ > 240 && span > 0.3f) {
        asserted_intact_ = true;
        if (span < 3.0f && finite) {
          say(std::format("the ragdoll is INTACT — {:.2f} m across, no "
                          "explosion (GEA 13.5.3.8's failure mode)",
                          span)
                  .c_str());
        } else {
          LogWarn(
              "infiltration: the ragdoll BLEW APART — {:.2f} m across, which "
              "is 13.5.3.8's failure mode and not a sprawl",
              span);
        }
      }
      // It FELL. A standing pelvis is about 1 m up; a body on the floor is not.
      if (!asserted_fell_ && steps_ - rag_[g].limp_step > 60) {
        asserted_fell_ = true;
        if (pelvis.y < kFallenPelvis && finite) {
          say(std::format("the body FELL — pelvis {:.2f} m, GEA 12.9's stage 5 "
                          "serving its consumer at last",
                          pelvis.y)
                  .c_str());
        } else {
          LogWarn(
              "infiltration: the body did NOT fall — pelvis still at {:.2f} m "
              "a second after going limp, so nothing took over its motion",
              pelvis.y);
        }
      }
    }
    // §13.4.8.8's fade completed, which is what §13.5.3.8 asks for in place of
    // a pose blend.
    for (Usize g = 0; g < kGuards && !asserted_fade_; ++g) {
      if (rag_[g].live && rag_[g].power <= 0.0f) {
        asserted_fade_ = true;
        say("the powered constraints FADED to zero — the body let go rather "
            "than being blended into a stranger's pose");
      }
    }
    if (!asserted_takedown_) {
      for (Usize g = 0; g < kGuards; ++g) {
        if (guard_.disabled[g]) {
          asserted_takedown_ = true;
          say(std::format("guard {} is DISABLED from behind (g4)", g).c_str());
        }
      }
    }
    // h2 — the hit boxes TRACK THEIR JOINTS. 480 samples is 30 bodies over the
    // first 16 steps, before anything is limp.
    if (!asserted_tracking_ && hit_box_samples_ >= 480) {
      asserted_tracking_ = true;
      if (hit_box_drift_ < 0.005f) {
        say(std::format("the HIT BOXES track their joints — worst drift {:.5f} "
                        "m over {} body-steps, game-driven (GEA 13.5.3.6)",
                        hit_box_drift_, hit_box_samples_)
                .c_str());
      } else {
        LogWarn(
            "infiltration: the hit boxes DRIFTED {:.4f} m from their joints "
            "over {} body-steps — 13.5.1.2's impulse move is not arriving",
            hit_box_drift_, hit_box_samples_);
      }
    }
    // h3 — the reach landed on a NAMED BONE, which is the whole difference
    // between a hit test and a distance check.
    if (!asserted_bone_ && !last_hit_bone_.empty() && last_hit_bone_ != "?") {
      asserted_bone_ = true;
      // AND IT LANDED AT REACH, which is the half that makes the DIRECTION
      // load-bearing. Aiming the ray straight up still lands a takedown
      // (mutation-proved 2026-09-10) because nothing stops the player walking
      // INTO a guard: the hit boxes are game-driven and push nobody, and
      // `nav::MoveCharacter` queries the level rather than the characters. So
      // the autopilot closes to 0.4 m and a vertical ray finds a thigh 0.11 m
      // up. A hit at arm's length is the evidence the aim mattered.
      if (last_hit_range_ > kMinReach) {
        say(std::format(
                "the takedown landed on a hit box called '{}' at {:.2f} "
                "m — 13.5.3.2's raycast, against 13.5.3.6's bodies",
                last_hit_bone_, last_hit_range_)
                .c_str());
      } else {
        LogWarn(
            "infiltration: the takedown landed on '{}' at only {:.2f} m — that "
            "is contact rather than reach, so the aim did not decide it",
            last_hit_bone_, last_hit_range_);
      }
    }
    // h3's NEGATIVE half, and the one that proves the aim is load-bearing: the
    // same button, in reach, from the front, does nothing.
    if (!asserted_refused_ && takedown_refused_ > 0) {
      asserted_refused_ = true;
      say(std::format("a takedown IN REACH was REFUSED ({} attempts from the "
                      "front) — being close enough is necessary and not "
                      "sufficient",
                      takedown_refused_)
              .c_str());
    }
    // h3 — AND IT LANDED FROM BEHIND, asserted where the takedown is over
    // rather than where it is decided. "It did not end in a takedown" is what a
    // broken button looks like; "it ended in one from the wrong side" is a
    // different failure and needs its own number.
    if (!asserted_behind_ && takedown_dot_ < 2.0f) {
      asserted_behind_ = true;
      if (takedown_dot_ <= kTakedownBehind) {
        say(std::format("the takedown that LANDED was from behind — dot {:.2f} "
                        "against a cutoff of {:.2f}",
                        takedown_dot_, kTakedownBehind)
                .c_str());
      } else {
        LogWarn(
            "infiltration: a takedown landed from the FRONT — dot {:.2f} "
            "against a cutoff of {:.2f}, so the approach no longer decides it",
            takedown_dot_, kTakedownBehind);
      }
    }
    // b2 — the player never stands inside a guard. Measured every step over the
    // whole run, because a separation checked once is also what a character
    // that tunnelled through and came back would report. A LIVE guard only:
    // a downed one is limp on the floor and is deliberately not a blocker.
    for (Usize g = 0; g < kGuards; ++g) {
      if (guard_.disabled[g]) {
        continue;
      }
      const Vec3 to = crowd_.AgentPosition(guard_.id[g]) - player_.position;
      closest_guard_ = std::min(closest_guard_, Length(Vec3{to.x, 0.0f, to.z}));
    }
    if (!asserted_apart_ && steps_ > 400) {
      asserted_apart_ = true;
      // The two radii, less a tolerance for the one step of motion the probe
      // is allowed before the sweep stops it.
      constexpr F32 kTouching = kGuardRadius + 0.35f - 0.1f;
      if (closest_guard_ >= kTouching) {
        say(std::format("the player never STOOD INSIDE a guard — closest "
                        "approach {:.2f} m against two radii of {:.2f} (GEA "
                        "13.5.3.6's capsule, not its hit boxes)",
                        closest_guard_, kGuardRadius + 0.35f)
                .c_str());
      } else {
        LogWarn(
            "infiltration: the player got {:.2f} m from a guard's centre, "
            "inside the {:.2f} m the two capsules occupy",
            closest_guard_, kGuardRadius + 0.35f);
      }
    }
    // g6 — §13.5.3.7, and it is v1's own measurement inverted. Route 2 spent
    // 143 of 400 steps with the wall between the camera and the player, up to
    // 7.88 m past it; the requirement is that the same run spends NONE.
    //
    // THE SECOND CLAUSE IS THE ONE THAT MAKES IT A GATE. Zero occlusions is
    // also what a camera welded at `min_distance` reports, staring at the
    // player's shoulder for the whole run — so the camera must also spend most
    // of the run at the distance it actually asked for.
    if (!asserted_camera_ && probe_.steps > 400) {
      asserted_camera_ = true;
      const F32 free_share =
          static_cast<F32>(probe_.free) / static_cast<F32>(probe_.steps);
      if (probe_.occluded == 0 && free_share > 0.5f) {
        say(std::format(
                "the CAMERA never sat behind the level — 0 of {} steps "
                "occluded, {:.0f}% of them at the distance it asked for (GEA "
                "13.5.3.7, and 17.2.2's third follow-camera part)",
                probe_.steps, free_share * 100.0f)
                .c_str());
      } else {
        LogWarn(
            "infiltration: the CAMERA sat behind the level for {} of {} steps, "
            "worst overshoot {:.2f} m, and was at its desired distance {:.0f}% "
            "of the time",
            probe_.occluded, probe_.steps, probe_.worst, free_share * 100.0f);
      }
    }
    // h4 — THE BODY GOES LIMP AT WALKING SPEED, which is the whole payoff of
    // switching a motion type instead of building bodies. §13.5.1.2's impulse
    // move leaves each hit box travelling at the speed that carried it to its
    // pose, so a guard walking at ~1.4 m/s keeps going as it collapses. Bodies
    // constructed at this instant — or driven by a teleport — would start from
    // a dead stop, and NOTHING ELSE HERE CAN SEE THAT: a teleported hit box
    // still tracks its joint exactly, and the ragdoll still falls and still
    // stays intact.
    for (Usize g = 0; g < kGuards && !asserted_inherited_; ++g) {
      if (!rag_[g].live || steps_ - rag_[g].limp_step != 12) {
        continue;
      }
      asserted_inherited_ = true;
      const Vec3 now = body_world_.BodyTransform(rag_[g].bodies[0]).position;
      const Vec3 travel = now - rag_[g].limp_from;
      const F32 flat = Length(Vec3{travel.x, 0.0f, travel.z});
      if (flat > kInheritedTravel) {
        say(std::format("the limp body CARRIED THE WALK — {:.3f} m of "
                        "horizontal travel in the first 0.2 s, inherited from "
                        "13.5.1.2's impulse move rather than dropped from rest",
                        flat)
                .c_str());
      } else {
        LogWarn(
            "infiltration: the limp body started from REST — {:.3f} m in 0.2 "
            "s, "
            "so the switch threw away the animation's velocity",
            flat);
      }
    }
    // THE CHECK THAT "DISABLED" REACHED PERCEPTION AND NOT JUST THE RENDERER.
    // A guard drawn grey while `UpdateAwareness` still runs on it would look
    // taken down and still catch you, which is exactly the class of defect the
    // three wrong assertions in ADR-0193 were. So: stand IN FRONT of a downed
    // guard, inside its cone's range, and require the confidence to stay at
    // zero for a full second of fixed steps.
    if (!asserted_blind_) {
      bool watching = false;
      for (Usize g = 0; g < kGuards; ++g) {
        const Vec3 at = crowd_.AgentPosition(guard_.id[g]);
        const Vec3 to = player_.position - at;
        const F32 range = Length(to);
        const Vec3 forward{std::sin(guard_.loco[g].facing), 0.0f,
                           std::cos(guard_.loco[g].facing)};
        if (guard_.disabled[g] && range > 1e-4f && range < 4.0f &&
            Dot(forward, to / range) > 0.3f) {
          watching = true;
          blind_steps_ = guard_.aware[g].confidence == 0.0f ? blind_steps_ + 1 : 0;
          if (blind_steps_ >= 60) {
            asserted_blind_ = true;
            say("a DOWNED guard held confidence at 0.00 for a second with the "
                "player square in front of it — 'disabled' reached perception");
          }
        }
      }
      if (!watching) {
        blind_steps_ = 0;
      }
    }
    if (!asserted_outcome_ && outcome_ != Outcome::kRunning) {
      asserted_outcome_ = true;
      say(outcome_ == Outcome::kCompleted
              ? "the run reached COMPLETED — GEA 16.10's flow FSM closed"
              : "the run reached CAUGHT — the fail state EXECUTED");
    }
    // §16.10's retry rule, checked rather than assumed. `steps_` is zeroed by
    // ResetRun and incremented before this runs, so 1 is the first step of the
    // second attempt; the player has moved by ~5 mm of the first acceleration
    // step, hence a radius rather than an equality.
    if (!asserted_retry_ && retries_ > 0 && steps_ == 1 &&
        Length(player_.position - kStart) < 0.1f && exposure_ < 0.05f) {
      asserted_retry_ = true;
      say(std::format("RETRY {} restored the start state — 'back to the "
                      "beginning of the current state'",
                      retries_)
              .c_str());
    }
    // GEA §13.5.3.6's second and third bullets, on the route rather than in a
    // unit test. The kerb strip is the only way round the wall's open end, so
    // every run crosses it twice.
    //
    // AIRBORNE IS COUNTED, NOT JUST THE HEIGHT, and the counting half is the
    // one that means something: climbing 0.25 m proves the step UP, and never
    // reporting a fall while over the strip proves the SNAP — which is the
    // whole of bullet 3, and the half a "did it get there" check cannot see.
    const bool over_kerb = player_.position.x > kKerbX0 && player_.position.x < kKerbX1;
    if (over_kerb && !player_.motion.grounded) {
      ++kerb_airborne_;
    }
    // THE HEIGHT INVARIANT, and it is here because a WIREFRAME CANNOT SETTLE
    // IT. This example already rendered its whole world 84 m in the air behind
    // ten green numeric assertions (event:2026-09-08#54), and a perspective
    // debug view of a stick figure on a wireframe floor is exactly as
    // inconclusive the second time. The controller owns `position.y` now, so
    // the floor and the kerb are the only two heights it may report.
    if (player_.position.y < -0.05f || player_.position.y > kKerbTop + 0.05f) {
      ++off_ground_;
    }
    if (!asserted_grounded_ && steps_ > 600) {
      asserted_grounded_ = true;
      if (off_ground_ == 0) {
        say(std::format("the player STAYED ON THE GROUND for {} steps — every "
                        "height was the floor or the {:.2f} m kerb",
                        steps_, kKerbTop)
                .c_str());
      } else {
        LogWarn(
            "infiltration: the player was off the ground for {} of {} "
            "steps — the controller is placing it wrong",
            off_ground_, steps_);
      }
    }
    if (!asserted_kerb_ && player_.motion.stepped_up && over_kerb) {
      asserted_kerb_ = true;
      say(std::format("the player STEPPED UP onto the {:.2f} m kerb — "
                      "max_climb {:.2f} m accepted it",
                      player_.position.y, player_.body.max_climb)
              .c_str());
    }
    // EITHER SIDE, and the first version said `x > kKerbX1`. The route turns
    // north on top of the strip and comes back down the WEST face, so an
    // assertion naming one side could not fire — the same "a check that cannot
    // fail" shape this example has now recorded three times.
    if (!asserted_kerb_down_ && asserted_kerb_ && !over_kerb &&
        player_.motion.grounded && player_.position.y < kKerbTop * 0.5f) {
      asserted_kerb_down_ = true;
      say(std::format("the player STEPPED DOWN off it without a fall — {} "
                      "airborne frames over the strip",
                      kerb_airborne_)
              .c_str());
    }
  }

  [[nodiscard]] anim::LocomotionConfig LocoConfig() const {
    return anim::LocomotionConfig{.gaits = gaits_, .turn_rate = 6.0f};
  }


  // The base pose for one actor, from its locomotion state — the four calls
  // ADR-0184/0192 describe, with the gesture layer added by the caller.



  void DrawGuards(RenderFrame& frame) {
    if (gaits_.empty()) {
      return;
    }
    for (Usize g = 0; g < kGuards; ++g) {
      // A DOWNED GUARD IS DRAWN FROM THE SAME FROZEN STATE — the phase and the
      // locomotion state stopped advancing, so `PoseFor` reproduces its last
      // pose exactly. Which is the point: it stands up straight, in mid-stride,
      // and that is what a ragdoll would fix.
      PoseFor(pose_, clips_->Skeleton(), agent_->Skeleton(), gaits_, map_,
              guard_.loco[g], guard_.phase[g]);
      // §12.10.2.5's MASKED gesture layer, at the weight the POLICY chose.
      const F32 posture = static_cast<F32>(std::clamp(
          boards_.Agent(g)->GetNumber(StringId{"posture"}, 0.0), 0.0, 1.0));
      if (posture > 0.0f && !alert_pose_.empty()) {
        anim::AddPose(pose_.local, alert_pose_, posture, pose_.local, alert_mask_);
      }
      // A LIMP GUARD IS POSED FROM ITS BODIES (§16.6.3's
      // ApplyRagDollsToSkeletons), and drawn in the frame frozen at the
      // takedown — the simulation's displacement lands in the pelvis's local
      // pose, so a fixed placement is correct and a chasing one would not be.
      if (rag_[g].live) {
        PoseFromLimp(pose_, rag_bodies_, agent_->Skeleton(), rag_rig_,
                     body_world_, rag_[g]);
        DrawSkeleton(frame, agent_->Skeleton(), pose_.local, pose_.globals, rag_[g].at, rag_[g].facing, kDownBone);
        continue;
      }
      const Vec3 at = crowd_.AgentPosition(guard_.id[g]);
      DrawSkeleton(frame, agent_->Skeleton(), pose_.local, pose_.globals, at, guard_.loco[g].facing,
                   guard_.disabled[g] ? kDownBone : kGuardBone);
      if (guard_.disabled[g]) {
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
            guard_.loco[g].facing + (t * 2.0f - 1.0f) * cone_.half_angle;
        const Vec3 rim =
            eye + Vec3{std::sin(a), 0.0f, std::cos(a)} * cone_.range;
        if (k == 0 || k == kArc) {
          frame.debug.AddLine(eye, rim, kCone);
        }
        if (k > 0) {
          frame.debug.AddLine(previous, rim, kCone);
        }
        previous = rim;
      }
      const ai::Awareness& aw = guard_.aware[g];
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


  // The objective, in the WORLD as well as on the HUD — §17.2.3's test again:
  // a goal a player cannot see is a goal they are guessing at.


  void BuildPanel() {
    auto& player = inspector_.AddSection("Player (GEA 17.2.1)");
    player.Slider("walk m/s",
                  inspector::Bind<F32>([this] { return player_.tune.walk_speed; },
                                       [this](F32 v) { player_.tune.walk_speed = v; }),
                  0.5f, 8.0f, 0.1f);
    player.Label("speed", [this] {
      return std::format("{:.2f} m/s", Length(player_.velocity));
    });
    player.Label("gait", [this] {
      return player_.loco.idle
                 ? std::string("idle")
                 : std::format("{} -> {} @ {:.2f}", player_.loco.gait_a,
                               player_.loco.gait_b, player_.loco.rate);
    });

    auto& heard = inspector_.AddSection("Heard (GEA 14.4.5.3)");
    heard.Label("path",
                [this] { return std::string(StateName(heard_[0].state)); });
    heard.Label("dry cutoff", [this] {
      return audio::CutoffIsOpen(heard_[0].dry_cutoff_hz)
                 ? std::string("open")
                 : std::format("{:.0f} Hz", heard_[0].dry_cutoff_hz);
    });

    auto& seen = inspector_.AddSection("Guards (GEA 17.2.3)");
    for (Usize g = 0; g < kGuards; ++g) {
      seen.Label(g == 0 ? "guard 0" : "guard 1", [this, g] {
        return std::format(
            "{} conf {:.2f} posture {:.2f}",
            boards_.Agent(g)->GetText(StringId{"state"}, "?"),
            guard_.aware[g].confidence,
            boards_.Agent(g)->GetNumber(StringId{"posture"}, 0.0));
      });
    }

    auto& flow = inspector_.AddSection("Flow (GEA 16.10)");
    flow.Label("state", [this] {
      return outcome_ == Outcome::kCompleted
                 ? std::string("completed")
                 : (outcome_ == Outcome::kCaught ? std::string("caught")
                                                 : std::string("playing"));
    });
    flow.Label("objective", [this] {
      return std::format(
          "leg {} of 2, {:.1f} m to go", leg_,
          Length(player_.position - (leg_ == 1 ? kObjective : kExtraction)));
    });
    flow.Label("exposure", [this] { return std::format("{:.2f}", exposure_); });
    flow.Label("retries", [this] { return std::format("{}", retries_); });
  }

  scene::Scene scene_;
  ui::Context ui_;
  examples::OrbitInput orbit_input_;
  scene::OrbitComponent* orbit_ = nullptr;
  resources::ResourceHandle<resources::Model> clips_;
  resources::ResourceHandle<resources::Model> agent_;
  Level level_;
  nav::NavMesh mesh_;
  nav::Crowd crowd_;
  WallQuery walls_;
  PlayerState player_;

  // the player
  int autopilot_ = 0;  // 0 = hands; 1 = the winning route; 2 = the losing one
  U64 steps_ = 0;
  Usize auto_leg_ = 0;
  F32 auto_seconds_ = 0.0f;
  F32 auto_hold_ = -1.0f;

  // the loop (g1-g3)
  FlowStack screens_;
  Outcome outcome_ = Outcome::kRunning;
  int leg_ = 1;  // 1 = out to the objective, 2 = back to the extraction
  F32 exposure_ = 0.0f;
  U32 retries_ = 0;
  Vec3 camera_pivot_{};
  scene::NodeId camera_node_{};
  CameraProbe probe_;
  resources::ResourceHandle<resources::Texture> white_;
  std::shared_ptr<const resources::Font> font_;

  // the guards
  GuardState guard_;

  // the ragdoll (r4-r6)
  physics::World body_world_;
  anim::RagdollRig rag_rig_;
  std::array<Limp, kGuards> rag_{};
  std::vector<nav::CharacterCapsule> capsules_;
  std::vector<Transform> rag_targets_;
  // What the last takedown's ray actually hit — reported, and asserted on.
  std::string last_hit_bone_;
  F32 last_hit_range_ = 0.0f;
  std::vector<Transform> rag_bodies_;
  ai::VisionCone cone_{.range = 18.0f, .half_angle = 0.7f};
  ai::AgentBoards boards_{kGuards};
  std::unique_ptr<app::AgentBindings> bindings_;
  std::unique_ptr<script::Vm> policy_;

  // animation
  anim::RetargetMap map_;
  std::vector<anim::LocomotionGait> gaits_;
  std::vector<Transform> clip_rest_;
  std::vector<Transform> agent_rest_;
  std::vector<Transform> alert_pose_;
  std::vector<F32> alert_mask_;
  PoseBuffers pose_;

  // audio
  audio::AudioSystem* audio_ = nullptr;
  resources::ResourceHandle<resources::AudioClip> step_clip_;
  std::array<audio::VoiceHandle, kGuards> voices_{};
  std::array<audio::VoicePropagation, kGuards> heard_{};

  bool asserted_muffled_ = false;
  bool asserted_seen_ = false;
  bool asserted_posture_ = false;
  bool asserted_open_ = false;
  bool asserted_lost_ = false;
  bool asserted_meter_ = false;
  bool asserted_leg_ = false;
  bool asserted_takedown_ = false;
  bool asserted_blind_ = false;
  bool asserted_outcome_ = false;
  bool asserted_retry_ = false;
  bool asserted_fell_ = false;
  bool asserted_tracking_ = false;
  bool asserted_bone_ = false;
  bool asserted_refused_ = false;
  bool asserted_inherited_ = false;
  bool asserted_behind_ = false;
  bool asserted_camera_ = false;
  bool asserted_apart_ = false;
  F32 closest_guard_ = 99.0f;
  F32 takedown_dot_ = 2.0f;  // 2 = no takedown has landed
  F32 hit_box_drift_ = 0.0f;
  Usize hit_box_samples_ = 0;
  Usize takedown_refused_ = 0;
  bool asserted_intact_ = false;
  bool asserted_fade_ = false;
  bool asserted_kerb_ = false;
  bool asserted_kerb_down_ = false;
  bool asserted_grounded_ = false;
  U32 off_ground_ = 0;
  U32 kerb_airborne_ = 0;
  U32 blind_steps_ = 0;
};

// --- the flow screens' bodies (§16.10) ---------------------------------------
// Out of line because they drive `Infiltration`, which is declared above them —
// the same shape `examples/sandbox` uses for the same reason.

void PlayingScreen::Update(const app::AppContext& ctx, F32 dt) {
  game_.StepWorld(ctx, dt);
  if (game_.Finished() && !handed_over_) {
    handed_over_ = true;
    Stack().Push(std::make_unique<OutcomeScreen>(game_, game_.Won()));
  }
}

void PlayingScreen::BuildUi(const app::AppContext& /*ctx*/, ui::Context& ui) {
  game_.DrawHud(ui);
}

void OutcomeScreen::Retry() {
  if (leaving_) {
    return;  // Update and HandleInput can both fire in one frame
  }
  leaving_ = true;
  game_.ResetRun();
  Stack().Pop();  // back onto the run underneath, which never left the stack
}

void OutcomeScreen::HandleInput(const app::AppContext& ctx) {
  if (ctx.input.JustPressed(platform::Key::kR) ||
      ctx.input.JustPressed(platform::Key::kEnter)) {
    Retry();
  }
}

void OutcomeScreen::Update(const app::AppContext& /*ctx*/, F32 dt) {
  shown_ += dt;
  // The autopilot retries only a FAILED run, so route 2 asserts the retry and
  // route 1 stops on its win instead of looping forever.
  if (game_.Autopilot() && !won_ && shown_ > 0.75f) {
    Retry();
  }
}

void OutcomeScreen::BuildUi(const app::AppContext& /*ctx*/, ui::Context& ui) {
  const Vec2 canvas = ui.Canvas();
  ui.Panel(ui::Rect{.x = 0.0f, .y = 0.0f, .w = canvas.x, .h = canvas.y},
           kScrim);
  const ui::Rect card = ui.Anchored(ui::Anchor::kCenter, 300.0f, 160.0f);
  ui.Panel(card, kHudBack);
  const F32 middle = card.x + card.w * 0.5f;
  ui.LabelCentered(middle, card.y + 24.0f, won_ ? "EXTRACTED" : "SPOTTED",
                   won_ ? kMeterCalm : kMeterHot);
  ui.LabelCentered(middle, card.y + 62.0f,
                   won_ ? "the objective is out" : "a guard has you", kHudText);
  const bool live = !IsDismissed() && !leaving_;
  if (ui.Button(ui::Rect{.x = card.x + 60.0f,
                         .y = card.y + 102.0f,
                         .w = card.w - 120.0f,
                         .h = 40.0f},
                won_ ? "Again  (R)" : "Retry  (R)") &&
      live) {
    Retry();
  }
}

}  // namespace

// Its own `main` now, where the examples harness used to supply one: this is
// a game in its own repository, not an entry in the example hub. The slice
// above is UNCHANGED — i2 splits it into runtime/features/view, and doing that
// in the same step as the move would make a behaviour change unattributable.
int main(int argc, char** argv) {
  Config config = app::LoadAppConfig(AETHER_CONFIG_DIR, "infiltration.json");
  config.Set("asset_dir", AETHER_ASSET_DIR);
  int autopilot = 0;
  app::CommandLine cli("infiltration");
  cli.Option("--autopilot", &autopilot,
             "1 = the winning route (takedown + objective); 2 = the losing "
             "one (walk into a guard's face, get caught, retry)");
  if (auto code = cli.ParseOrExit(argc, argv, config)) {
    return *code;
  }
  const auto result =
      app::Run(std::make_unique<Infiltration>(autopilot), config);
  if (!result) {
    LogError("infiltration failed [{}]: {}",
             app::ToString(result.error().error),
             result.error().cause.message);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
