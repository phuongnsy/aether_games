#include "lantern.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "aether/audio/audio_system.hpp"
#include "aether/core/log.hpp"
#include "aether/core/math/geometry.hpp"
#include "aether/core/math/quat.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/platform/window.hpp"
#include "aether/resources/world_chunk.hpp"
#include "aether/rhi/device.hpp"
#include "aether/scene/mesh_component.hpp"
#include "aether/scene/world_instance.hpp"
#include "aether/scene_core/camera_component.hpp"
#include "aether/scene_core/collision3.hpp"
#include "aether/scene_core/punctual_light_component.hpp"
#include "lantern/content/spire.hpp"
#include "lantern/view/hud.hpp"

namespace lantern::app {

using namespace aether;

namespace {
// The camera sits behind and above, looking at the walker's chest. Fixed rather
// than orbiting: a climb reads as vertical progress, and a camera the player
// can spin hides how far they have come.
constexpr F32 kCameraLag = 6.0f;
constexpr F32 kCameraMinDistance =
    1.6f;  // never closer than this, occluded or not
constexpr F32 kCameraMaxDistance = 18.0f;
constexpr F32 kCameraSensitivity = 0.006f;  // radians per pixel of drag
constexpr F32 kPitchMin = -0.25f;           // just below level
constexpr F32 kPitchMax = 1.15f;  // short of straight down, which gimbals
constexpr F32 kChestY = 1.0f;
constexpr F32 kCameraProbe = 0.3f;
constexpr F32 kCameraSkin = 0.06f;
constexpr F32 kWalkThreshold = 0.4f;  // m/s below which "walk" reads as idle
constexpr F32 kScriptDt = 1.0f / 60.0f;

// The scripted route to the first lantern: walk to it, then light it. Short on
// purpose — this exists to make composition check 4 (a light added at RUNTIME
// re-solves the shadow atlas) verifiable without hands, not to play the game.
struct ScriptStep {
  F32 seconds;
  Vec2 move;
  bool interact;
};
constexpr std::array<ScriptStep, 6> kScript = {{
    // AXIS-ALIGNED LEGS, ROUTED AROUND THE SPIRE. The first attempt walked a
    // straight diagonal at the lantern and stopped 1.6 m later against the
    // tier-3 block — the controller was right and the route was naive. Legs
    // are timed from the walk speed (4.2 m/s) and the distances in the world.
    {.seconds = 0.4f,
     .move = Vec2{0.0f, 0.0f},
     .interact = false},  // settle onto the ground
    {.seconds = 1.4f,
     .move = Vec2{1.0f, 0.0f},
     .interact = false},  // +x, clear of the tier-4 block
    {.seconds = 1.8f,
     .move = Vec2{0.0f, -1.0f},
     .interact = false},  // -z, down the outside
    {.seconds = 0.6f,
     .move = Vec2{-1.0f, 0.0f},
     .interact = false},  // -x, stepping onto tier 1
    {.seconds = 0.3f,
     .move = Vec2{0.0f, 0.0f},
     .interact = false},  // stop under the lantern
    {.seconds = 0.2f, .move = Vec2{0.0f, 0.0f}, .interact = true},  // light it
}};

// A rotation that faces `dir`, built from yaw then pitch — the engine has no
// look-at quaternion and this needs no general one: a camera and a walker both
// have an up that never tilts, so roll is not a degree of freedom here.
// Composed Y then X, matching the order the world spawner applies `rot` in.
[[nodiscard]] Quat FacingRotation(Vec3 dir, bool with_pitch) {
  const F32 length = Length(dir);
  if (length < 1e-5f) {
    return Quat{};
  }
  const Vec3 n = dir / length;
  // Forward is -Z, so yaw is measured against the negated direction.
  const F32 yaw = std::atan2(-n.x, -n.z);
  const Quat spin = QuatFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yaw);
  if (!with_pitch) {
    return spin;
  }
  const F32 pitch = std::asin(std::clamp(n.y, -1.0f, 1.0f));
  return spin * QuatFromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitch);
}
}  // namespace

aether::app::RenderPipeline LanternGame::BuildPipeline() {
  // The DEFAULT pipeline's two passes, with a depth prepass in front when soft
  // particles are on — a forward pass cannot produce its own depth input.
  // Spelled out rather than extending DefaultPipeline(), because a pipeline
  // that silently inherits a changed default is worse than one that states it.
  aether::app::RenderPipeline pipeline;
  pipeline.AddPass(
      StringId::FromRuntime("precip_depth"),
      [this](const aether::app::FrameContext& c) {
        // No precipitation ⇒ nothing to fade, so do not pay for
        // the prepass. This is what keeps `--weather off` — the
        // base capture oracle — free of the extra geometry pass.
        if (!soft_particles_ || weather_mode_ == weather::Mode::kOff) {
          c.app.renderer.SetSoftParticleDepth({}, 0.0f);
          return;
        }
        c.app.renderer.SetSoftParticleDepth(
            c.app.renderer.RenderDepthPrepass(c.frame), soft_fade_);
      });
  pipeline.AddPass(aether::app::ScenePass())
      .AddPass(aether::app::UpscalePass());
  return pipeline;
}

Result<void> LanternGame::Load(const aether::app::AppContext& ctx) {
  materials_ = std::make_unique<aether::app::PbrMaterialFactory>(
      ctx.renderer, aether::app::MaterialSharing::kShared);
  spawners_ =
      std::make_unique<aether::app::EngineSpawners>(ctx.resources, *materials_);

  auto text = ctx.assets.ReadText(content::kWorldPath);
  if (!text) {
    return std::unexpected(text.error());
  }
  auto chunk = resources::ParseWorldChunk(*text, content::kWorldPath);
  if (!chunk) {
    return std::unexpected(chunk.error());
  }
  auto instance =
      scene::InstantiateChunk(scene_, scene_.Root(), *chunk, *spawners_);
  if (!instance) {
    return std::unexpected(instance.error());
  }

  // PUBLISH WORLD TRANSFORMS BEFORE READING THEM. `Node::world` is a cache
  // filled by PropagateTransforms, which runs inside BuildRenderFrame — NOT
  // inside Update. So reading it after InstantiateChunk hands back identity:
  // every lantern registered at the origin, nothing was ever in reach, and the
  // failure presented as a reach rule rather than an unpublished transform.
  // One discarded frame is the cheapest honest way to ask for the real answer.
  (void)scene_.BuildRenderFrame(Viewport{.width = 1, .height = 1});

  // The lanterns are found by ASKING THE SCENE what they are, not by index into
  // the file — adding an entity must not silently renumber them.
  std::vector<scene::NodeId> lantern_nodes;
  for (const scene::NodeId id : instance->nodes) {
    auto* light = scene_.GetComponent<scene::PunctualLightComponent>(id);
    if (light == nullptr || light->Type() != LightType::kSpot) {
      continue;
    }
    const scene::Node* node = scene_.Get(id);
    world_.Lanterns().Add(TransformPoint(node->world, Vec3{}));
    lantern_nodes.push_back(id);
  }

  // Weather: each platform is a mesh + collider PAIR at identical pos (the
  // world file's own contract), so the collider — whose node id the impacts
  // report — is matched to its mesh by position, the same ask-the-scene rule
  // the lanterns use. The patch is the collider's TOP face.
  if (auto fx = weather_fx_.Load(ctx.device); !fx) {
    return fx;
  }
  world_.Weather().Setup(scene_, weather_mode_, /*seed=*/0x1a27u,
                         weather_fx_.RainTexture(), weather_fx_.SnowTexture());
  for (const scene::NodeId id : instance->nodes) {
    auto* col = scene_.GetComponent<scene::Collider3Component>(id);
    if (col == nullptr || col->kind != scene::Shape3Kind::kBox) {
      continue;
    }
    const Vec3 at = TransformPoint(scene_.Get(id)->world, Vec3{});
    const Vec3 half = col->half_extents;
    // ~0.5 m grid cells, and at least 3 so the brush has neighbours.
    const auto cells = [](F32 extent) {
      return std::max(3u, static_cast<U32>(extent * 4.0f) + 1u);
    };
    world_.Weather().RegisterPlatform(
        static_cast<U64>(id.id),
        Vec3{at.x - half.x, at.y + half.y + 0.02f, at.z - half.z},
        Vec3{2.0f * half.x, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 2.0f * half.z},
        cells(half.x), cells(half.z));
    for (const scene::NodeId mesh_id : instance->nodes) {
      if (scene_.GetComponent<scene::MeshComponent>(mesh_id) == nullptr) {
        continue;
      }
      const Vec3 mesh_at = TransformPoint(scene_.Get(mesh_id)->world, Vec3{});
      if (LengthSquared(mesh_at - at) < 1e-4f) {
        wet_surfaces_.emplace_back(static_cast<U64>(id.id), mesh_id);
        break;
      }
    }
  }
  if (lantern_nodes.empty()) {
    return Fail(Errc::kInvalidArgument,
                "the spire has no lanterns — every check here needs one");
  }
  lights_.Bind(std::move(lantern_nodes));

  auto model = ctx.resources.Load<resources::Model>(content::kCharacterPath);
  if (!model) {
    return std::unexpected(model.error());
  }
  character_ = *model;
  body_ = scene_.CreateNode(scene_.Root());
  skinned_ = scene_.AddComponent<scene::SkinnedMeshComponent>(body_);
  skinned_->mesh = character_->Meshes()[0].mesh;
  skinned_->model = character_;
  skinned_->BuildStates();
  clip_ = "idle";
  if (!skinned_->Snap(clip_)) {
    return Fail(Errc::kNotFound, "the character's idle clip would not play");
  }

  camera_ = scene_.CreateNode(scene_.Root());
  auto* camera = scene_.AddComponent<scene::CameraComponent>(camera_);
  // PERSPECTIVE, explicitly. The component defaults to orthographic, which at
  // metre scale renders a black screen that looks exactly like a lighting bug —
  // and cost a round of chasing one.
  camera->projection = ProjectionMode::kPerspective;
  camera->near_z = 0.1f;
  camera->far_z = 400.0f;
  camera->reference_size = 0.0f;
  scene_.SetActiveCamera(camera_);

  if (auto font = ctx.resources.Load<resources::Font>("fonts/hud.fnt")) {
    font_ = *font;
  }
  // Audio is OPTIONAL at load: a missing clip should cost the sound, not the
  // game. The engine already degrades a bad texture path to a visible
  // placeholder; silence is the audio equivalent.
  if (auto chime =
          ctx.resources.Load<resources::AudioClip>("audio/lantern_lit.wav")) {
    chime_ = *chime;
    // Says out loud that the engine's decoder agrees with what audio_studio
    // wrote. A malformed header fails behind the resources seam at RUNTIME, so
    // a duration that matches the authored 1.6 s is the cheapest proof the
    // studio -> miniaudio path is real rather than merely file-shaped.
    LogInfo("lantern: chime decoded, {:.2f}s", chime_->DurationSeconds());
  } else {
    LogWarn("lantern: no chime ({}); lighting will be silent",
            chime.error().message);
  }

  audio_ = &ctx.audio;
  for (int i = 0; i < lit_at_load_; ++i) {
    (void)world_.Lanterns().Light(static_cast<aether::Usize>(i));
  }
  world_.Reset(Vec3{content::kStartX, content::kStartY, content::kStartZ});
  LogInfo(
      "lantern: {} lantern(s) on the spire. WASD climb, space jump, E light, "
      "drag look, scroll zoom.",
      world_.Lanterns().Count());
  return {};
}

void LanternGame::DriveScript(F32 dt) {
  input_ = runtime::Input{};
  if (script_step_ >= kScript.size()) {
    return;
  }
  const ScriptStep& step = kScript[script_step_];
  input_.move = step.move;
  input_.interact = step.interact;
  script_time_ += dt;
  if (script_time_ >= step.seconds) {
    script_time_ = 0.0f;
    ++script_step_;
  }
}

void LanternGame::Update(const aether::app::AppContext& ctx, F32 dt) {
  last_dt_ = dt;
  if (demo_) {
    last_dt_ = kScriptDt;
    DriveScript(kScriptDt);
    world_.Step(scene_, input_, kScriptDt);
    input_.jump = false;
    input_.interact = false;
    DrainEvents();
    scene_.Update(kScriptDt);
    return;
  }
  // Input is LATCHED here and consumed by the fixed step, so a jump pressed
  // between steps is never dropped and never counted twice.
  // Drag orbits, scroll zooms. The cursor is not captured, so a drag is the
  // honest gesture — and it matches every explorer in the repo.
  if (ctx.input.IsMouseDown(platform::MouseButton::kLeft)) {
    const Vec2 drag = ctx.input.CursorDelta();
    cam_yaw_ -= drag.x * kCameraSensitivity;
    cam_pitch_ = std::clamp(cam_pitch_ + drag.y * kCameraSensitivity, kPitchMin,
                            kPitchMax);
  }
  if (const F32 scroll = ctx.input.ScrollDelta(); scroll != 0.0f) {
    cam_distance_ = std::clamp(cam_distance_ - scroll, kCameraMinDistance,
                               kCameraMaxDistance);
  }

  Vec2 move{};
  move.y += ctx.input.IsDown(platform::Key::kW) ? 1.0f : 0.0f;
  move.y -= ctx.input.IsDown(platform::Key::kS) ? 1.0f : 0.0f;
  move.x += ctx.input.IsDown(platform::Key::kD) ? 1.0f : 0.0f;
  move.x -= ctx.input.IsDown(platform::Key::kA) ? 1.0f : 0.0f;
  // CAMERA-RELATIVE, and this is what makes an orbiting camera usable at all:
  // with world-axis movement, W walks north regardless of where you are
  // looking, so orbiting behind the walker inverts the controls. Forward is
  // "away from the camera", which is the direction the player actually means.
  const F32 sin_yaw = std::sin(cam_yaw_);
  const F32 cos_yaw = std::cos(cam_yaw_);
  const Vec2 forward{-sin_yaw, -cos_yaw};
  const Vec2 right{cos_yaw, -sin_yaw};
  input_.move = Vec2{(forward.x * move.y) + (right.x * move.x),
                     (forward.y * move.y) + (right.y * move.x)};
  input_.jump = input_.jump || ctx.input.JustPressed(platform::Key::kSpace);
  input_.interact = input_.interact || ctx.input.JustPressed(platform::Key::kE);
}

void LanternGame::FixedUpdate(const aether::app::AppContext& /*ctx*/, F32 dt) {
  // Precipitation follows the VIEW, biased ahead of it: centred on the camera,
  // most of the box would sit behind the player where it is neither visible nor
  // able to settle on the ground they are looking at.
  constexpr F32 kSnowAhead = 10.0f;
  const Vec3 fwd =
      Rotate(scene_.Get(camera_)->local.rotation, Vec3{0.0f, 0.0f, -1.0f});
  const F32 flat = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
  const Vec3 eye = scene_.Get(camera_)->local.position;
  const Vec2 ahead =
      flat > 1e-3f ? Vec2{fwd.x / flat, fwd.z / flat} : Vec2{0.0f, -1.0f};
  world_.Weather().SetViewCenter(
      Vec2{eye.x + ahead.x * kSnowAhead, eye.z + ahead.y * kSnowAhead});

  // THE DEMO STEPS PER FRAME, NOT PER FIXED TICK. Making the script's dt fixed
  // was not enough: the engine calls FixedUpdate from a real-time accumulator,
  // so a fast headless run produced almost no ticks and the script never got
  // past its first step. `--frames N` must mean N sim steps, or a capture of
  // check 4 is once again measuring how fast the machine is.
  if (demo_) {
    return;  // Update() drives the sim in demo mode
  }
  world_.Step(scene_, input_, dt);
  input_.jump = false;
  input_.interact = false;
  DrainEvents();
  scene_.Update(dt);
}

void LanternGame::DrainEvents() {
  for (const runtime::GameEvent& event : world_.Frame()) {
    if (const auto* lit = std::get_if<runtime::LanternLit>(&event)) {
      LogInfo("lantern: lit {} of {}", world_.Lanterns().LitCount(),
              world_.Lanterns().Count());
      // AT THE LANTERN, not at the player. The whole point of a 3D mix is that
      // the sound belongs to the thing that made it — walk away afterwards and
      // it should fall behind you, which a 2D play would not do.
      if (chime_ != nullptr && audio_ != nullptr) {
        audio_->PlaySpatial(chime_,
                            audio::SpatialSource{.position = lit->position});
      }
    } else if (const auto* fell = std::get_if<runtime::Respawned>(&event)) {
      LogInfo("lantern: {}", fell->at_checkpoint ? "back to your last lantern"
                                                 : "back to the start");
    }
  }
}

void LanternGame::DriveClip() {
  const runtime::ViewSnapshot snapshot = world_.Snapshot();
  const char* want = !snapshot.grounded                ? "jump"
                     : snapshot.speed > kWalkThreshold ? "walk"
                                                       : "idle";
  if (clip_ != want) {
    clip_ = want;
    skinned_->Machine().Request(StringId::FromRuntime(clip_));
  }
}

void LanternGame::PlaceCamera(const aether::app::AppContext& /*ctx*/) {
  const Vec3 target = world_.Walker().Position() + Vec3{0.0f, kChestY, 0.0f};
  scene::Node* node = scene_.Get(camera_);

  // Spherical about the walker: yaw around Y, pitch above the horizon.
  const F32 cos_pitch = std::cos(cam_pitch_);
  const Vec3 dir{std::sin(cam_yaw_) * cos_pitch, std::sin(cam_pitch_),
                 std::cos(cam_yaw_) * cos_pitch};
  Vec3 want = target + (dir * cam_distance_);

  // PULL IN THROUGH GEOMETRY, but never past kCameraMinDistance. Stopping at
  // the contact alone put the camera on the walker's shoulders and the view
  // became a wall; a floor keeps a usable shot even when the ideal one is
  // blocked, and the player can orbit out of it.
  const Vec3 out = want - target;
  const F32 reach = Length(out);
  if (reach > 1e-4f) {
    const aether::scene::ShapeCastResult blocked = scene_.SphereCast(
        Sphere{.center = target, .radius = kCameraProbe}, out);
    if (blocked.Hit()) {
      const F32 hit_at = std::max(0.0f, blocked.t * reach - kCameraSkin);
      want = target + (out / reach) * std::max(hit_at, kCameraMinDistance);
    }
  }

  // Exponential follow, framerate-independent: a fixed lerp factor would be
  // stiffer at high frame rates and mushy at low ones.
  const F32 alpha = 1.0f - std::exp(-kCameraLag * std::max(last_dt_, 1e-4f));
  node->local.position =
      node->local.position + (want - node->local.position) * alpha;
  node->local.rotation =
      FacingRotation(target - node->local.position, /*with_pitch=*/true);
}

RenderFrame LanternGame::Extract(const aether::app::AppContext& ctx) {
  // GPU cost from the DEVICE's own timer, and only where the backend says it is
  // valid — an invalid timer reports 0.0, which would average into a
  // confidently wrong number rather than an obviously missing one.
  const rhi::FrameStats stats = ctx.device.GetFrameStats();
  if (const auto gpu = stats.GpuFrameMs()) {
    gpu_ms_total_ += *gpu;
    ++gpu_samples_;
  }
  draw_calls_ = stats.draw_calls;

  const runtime::ViewSnapshot snapshot = world_.Snapshot();
  lights_.Sync(scene_, snapshot);
  // The LISTENER is the walker, not the camera. A camera-mounted listener
  // swings the mix every time the player orbits, which is disorienting when
  // the character has not moved at all.
  if (audio_ != nullptr) {
    audio_->SetListener(audio::Listener{
        .position = snapshot.character,
        .orientation = FacingRotation(snapshot.facing, /*with_pitch=*/false)});
  }
  DriveClip();
  PlaceCamera(ctx);

  scene::Node* body = scene_.Get(body_);
  body->local.position = world_.Walker().Position() - Vec3{0.0f, 0.4f, 0.0f};
  body->local.rotation =
      FacingRotation(world_.Walker().Facing(), /*with_pitch=*/false);
  // The sim's per-surface wetness becomes each platform material's input —
  // presentation of sim state, same category as Lights::Sync above.
  for (const auto& [surface, mesh_id] : wet_surfaces_) {
    if (auto* mc = scene_.GetComponent<scene::MeshComponent>(mesh_id)) {
      mc->wetness = world_.Weather().Wetness().Wetness(surface);
    }
  }
  scene_.Update(0.0f);

  RenderFrame frame =
      scene_.BuildRenderFrame(Viewport{.width = ctx.render_size.width,
                                       .height = ctx.render_size.height},
                              ctx.ShowDebugBounds(), &ctx.jobs);
  const Usize before_precip = frame.items.size();
  weather_fx_.Emit(frame.items, frame.view.camera, world_.Weather());
  // Rain AND snow opt into the depth fade — both are billboards that can meet
  // the spire's geometry. Splashes and snow caps are deliberately NOT tagged:
  // they lie flat ON a surface, so fading them against that surface would
  // erase them.
  if (soft_particles_) {
    const MaterialHandle soft = ctx.renderer.SoftParticleMaterial();
    for (Usize i = before_precip; i < frame.items.size(); ++i) {
      frame.items[i].material = soft;
    }
  }
  return frame;
}

RenderFrame LanternGame::BuildOverlay(const aether::app::AppContext& ctx) {
  const Size fb = ctx.window.FramebufferSize();
  ui_.BeginFrame(ctx.input.CursorPosition(),
                 ctx.input.IsMouseDown(platform::MouseButton::kLeft), fb,
                 font_.get(), {});
  view::DrawHud(ui_, font_.get(), world_.Snapshot(),
                Vec2{static_cast<F32>(fb.width), static_cast<F32>(fb.height)});
  return ui_.EndFrame();
}

void LanternGame::Unload(const aether::app::AppContext& /*ctx*/) {
  if (gpu_samples_ > 0) {
    LogInfo("lantern: lit={} gpu={} us/frame draws={} ({} samples)",
            world_.Lanterns().LitCount(),
            static_cast<int>(1000.0 * gpu_ms_total_ /
                             static_cast<F64>(gpu_samples_)),
            draw_calls_, gpu_samples_);
  }
  scene_ = scene::Scene{};
  character_.reset();
  font_.reset();
  spawners_.reset();
  materials_.reset();
}

}  // namespace lantern::app
