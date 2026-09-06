#include "tideworn.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "aether/core/log.hpp"
#include "aether/render/renderer.hpp"
#include "aether/render/water.hpp"
#include "aether/resources/material.hpp"
#include "aether/scene/mesh_component.hpp"
#include "aether/scene_core/camera_component.hpp"
#include "aether/scene_core/punctual_light_component.hpp"
#include "tideworn/content/content.hpp"
#include "tideworn/view/sea.hpp"

namespace tideworn::app {

using namespace aether;

namespace {

// Sea level. One constant for the water plane, the buoyancy probe and the
// creature depths — two copies of this number is a drifting waterline.
constexpr F32 kSeaLevel = 0.0f;

// The sun's direction of TRAVEL. MUST stay in step with env_studio's `ocean`
// preset, which bakes its sun disk at exactly the negation of this — one
// physical sun written in two places (a radiance field cannot be computed at
// runtime); the water derives ITS sun from this light (ADR-0055).
const Vec3 kSunDir = Normalize(Vec3{0.86f, -0.44f, -0.26f});

// Smallest rotation taking +Y onto the surface normal (water_lab's TiltTo).
[[nodiscard]] Quat TiltTo(Vec3 normal) {
  const Vec3 up{0.0f, 1.0f, 0.0f};
  const Vec3 axis = Cross(up, normal);
  const F32 len = Length(axis);
  if (len < 1e-5f) {
    return Quat::Identity();
  }
  return QuatFromAxisAngle(axis * (1.0f / len),
                           std::asin(std::clamp(len, -1.0f, 1.0f)));
}

[[nodiscard]] Result<MaterialHandle> MakeMaterial(
    const aether::app::AppContext& ctx, const char* name, Vec4 color,
    F32 roughness) {
  resources::PbrParams params;
  params.base_color = color;
  params.metallic = 0.0f;
  params.roughness = roughness;
  const rhi::RenderState state{.blend = rhi::BlendMode::kOpaque,
                               .cull = rhi::CullMode::kNone,
                               .depth = rhi::RenderState::Depth::kTestWrite};
  return ctx.renderer.CreatePbrMaterial(
      resources::Material(name, params, resources::MaterialSlots{}, state));
}

[[nodiscard]] const char* BandName(voyage::Band band) {
  switch (band) {
    case voyage::Band::kCalm:
      return "calm";
    case voyage::Band::kFresh:
      return "fresh";
    case voyage::Band::kGale:
      return "gale";
    case voyage::Band::kStorm:
      return "storm";
  }
  return "?";
}

}  // namespace

aether::app::RenderPipeline TidewornGame::BuildPipeline() {
  aether::app::RenderPipeline pipeline;
  pipeline.AddPass(StringId::FromRuntime("skybox"),
                   [this](const aether::app::FrameContext& c) { DrawSky(c); });
  // Scene depth for the water column (thickness, absorption, contact foam) —
  // always on, it IS what makes open water read as deep. Fade 0.2: soft
  // particles blend where spray meets the sea — 0.5 swallowed the near-field
  // spray whole, because spray LIVES inside half a metre of the surface.
  pipeline.AddPass(StringId::FromRuntime("water_depth"),
                   [](const aether::app::FrameContext& c) {
                     c.app.renderer.SetSoftParticleDepth(
                         c.app.renderer.RenderDepthPrepass(c.frame), 0.2f);
                   });
  // The disturbance grid's upload — BEFORE the scene pass, whose Submit stages
  // the water bindings that carry the region. UpdateTexture is an API-thread
  // call, so the pixels arrive rung from Extract rather than computed here.
  pipeline.AddPass(
      StringId::FromRuntime("disturbance"),
      [this](const aether::app::FrameContext& c) {
        const FrameFx& fx = fx_ring_[c.frame_index % kFxRing];
        if (fx.grid_pixels.empty() || !disturbance_texture_.Valid()) {
          return;
        }
        c.app.device.UpdateTexture(
            disturbance_texture_, view::Disturbance::kResolution,
            view::Disturbance::kResolution, fx.grid_pixels.data(),
            static_cast<U32>(fx.grid_pixels.size()));
        c.app.renderer.SetWaterDisturbance(disturbance_texture_,
                                           fx.grid_region);
      });
  pipeline.AddPass(aether::app::HdrScenePass(/*clear=*/false));
  // The scene-colour copy, after the opaques and before the water samples it.
  pipeline.AddPass(StringId::FromRuntime("scene_copy"),
                   [this](const aether::app::FrameContext& c) {
                     scene_copy_.Ensure(c.app.device, c.app.render_size,
                                        rhi::TextureFormat::kRGBA16F);
                     c.app.renderer.Blit(c.target.color, scene_copy_.fb);
                     c.app.renderer.SetWaterSceneColor(scene_copy_.color);
                   });
  // CUBE-ONLY water (the plan's mirror policy): open sea reflects sky, and the
  // planar mirror's render-the-scene-twice cost buys nothing out here.
  pipeline.AddPass(StringId::FromRuntime("water"),
                   [this](const aether::app::FrameContext& c) {
                     const FrameFx& fx = fx_ring_[c.frame_index % kFxRing];
                     if (fx.underwater) {
                       // The surface from BELOW: fog + Snell's-window ceiling
                       // (the projected grid collapses under the plane by
                       // design, so the mesh has nothing to draw down here).
                       c.app.renderer.DrawUnderwater(c.target.fb, c.frame.view);
                     } else if (water_mesh_.Ready()) {
                       // Near the waterline the surface crosses the NEAR PLANE
                       // and the mesh clips against it — a hard tear across the
                       // frame. The fill fogs only that gap; the mesh, drawn
                       // over it, owns every surface point it can rasterise.
                       if (fx.near_surface) {
                         c.app.renderer.DrawUnderwater(c.target.fb,
                                                       c.frame.view,
                                                       /*straddle_fill=*/true);
                       }
                       water_mesh_.Draw(c.app.device, c.app.renderer,
                                        c.target.fb, c.frame.view);
                     }
                   });
  // AFTER the water: spray hangs in the air ABOVE the surface, and the water
  // pass composites over everything the scene pass drew (translucents write
  // no depth), so spray drawn there vanished under the sea it flew over.
  pipeline.AddPass(StringId::FromRuntime("spray"),
                   [this](const aether::app::FrameContext& c) {
                     const std::vector<Renderable>& items =
                         fx_ring_[c.frame_index % kFxRing].spray;
                     if (items.empty()) {
                       return;
                     }
                     RenderFrame overlay;
                     overlay.view = c.frame.view;
                     overlay.items = items;
                     c.app.renderer.Submit(overlay, c.target.fb, 0,
                                           /*clear=*/false);
                   });
  pipeline.AddPass(aether::app::TonemapPass(&tonemap_));
  return pipeline;
}

Result<void> TidewornGame::Load(const aether::app::AppContext& ctx) {
  // The time-of-day ring. All eight or nothing: a partial ring would blend
  // across a missing keyframe and read as a lighting glitch at a fixed hour.
  std::vector<view::SkyRing::Key> keys;
  keys.reserve(content::kSkyRing.size());
  for (const content::SkyKey& key : content::kSkyRing) {
    auto env = ctx.resources.Load<resources::Environment>(key.env);
    if (!env) {
      LogWarn(
          "tideworn: sky keyframe {} missing — falling back to the "
          "single-bake sky (run `pixi run assets`)",
          key.env);
      keys.clear();
      break;
    }
    keys.push_back(view::SkyRing::Key{.env = *env, .at = key.at});
  }
  sky_ring_.SetKeys(std::move(keys));
  if (auto env =
          ctx.resources.Load<resources::Environment>(content::kEnvPath)) {
    environment_ = *env;
  } else if (!sky_ring_.Ready()) {
    LogWarn("tideworn: no environment ({}) — the sea will reflect nothing",
            content::kEnvPath);
  }
  if (auto sky = skybox_.Create(ctx.device); !sky) {
    return sky;
  }
  if (auto water = water_mesh_.Create(ctx.device); !water) {
    return water;
  }
  if (auto spray = spray_.Load(ctx.device); !spray) {
    return spray;
  }
  if (auto rain = rain_.Load(ctx.device); !rain) {
    return rain;
  }
  auto grid_tex = ctx.device.CreateDynamicTexture(
      view::Disturbance::kResolution, view::Disturbance::kResolution,
      rhi::TextureFormat::kRGBA8);
  if (!grid_tex) {
    return Fail(Errc::kInitFailed, "disturbance texture create failed");
  }
  disturbance_texture_ = *grid_tex;

  auto hull = ctx.resources.Load<resources::Model>(content::kBoatPath);
  if (!hull) {
    return Fail(Errc::kInitFailed, "tideworn: boat model failed to load");
  }
  hull_ = *hull;
  auto hull_mat =
      MakeMaterial(ctx, "hull", Vec4{0.30f, 0.22f, 0.15f, 1.0f}, 0.72f);
  if (!hull_mat) {
    return Fail(Errc::kInitFailed, "tideworn: material create failed");
  }

  boat_ = scene_.CreateNode(scene_.Root());
  auto* bm = scene_.AddComponent<scene::MeshComponent>(boat_);
  bm->mesh = hull_->Meshes()[0].mesh;
  bm->material = *hull_mat;
  bm->casts_shadows = true;

  // Marine life: the rigged fish, one material per species — a silver shoal,
  // a warmer reef fish, a slate-dark deep rover.
  auto fish_model = ctx.resources.Load<resources::Model>(content::kFishPath);
  if (!fish_model) {
    return Fail(Errc::kInitFailed, "tideworn: fish model failed to load");
  }
  auto shoal =
      MakeMaterial(ctx, "shoal", Vec4{0.62f, 0.68f, 0.72f, 1.0f}, 0.35f);
  auto reef = MakeMaterial(ctx, "reef", Vec4{0.66f, 0.52f, 0.34f, 1.0f}, 0.5f);
  auto deep = MakeMaterial(ctx, "deep", Vec4{0.30f, 0.34f, 0.38f, 1.0f}, 0.6f);
  if (!shoal || !reef || !deep) {
    return Fail(Errc::kInitFailed, "tideworn: fish material create failed");
  }
  const std::array<MaterialHandle, 3> fish_mats{*shoal, *reef, *deep};
  if (auto life = creatures_.Load(scene_, *fish_model, fish_mats,
                                  world_.Snapshot().fish);
      !life) {
    return life;
  }

  // The DIVER: the third-person camera's body. Loaded up front even though
  // the game boots on deck — V can enter the water at any moment.
  auto diver_model = ctx.resources.Load<resources::Model>(content::kDiverPath);
  if (!diver_model) {
    return Fail(Errc::kInitFailed, "tideworn: diver model failed to load");
  }
  diver_model_ = *diver_model;
  auto wetsuit =
      MakeMaterial(ctx, "wetsuit", Vec4{0.12f, 0.16f, 0.22f, 1.0f}, 0.6f);
  if (!wetsuit) {
    return Fail(Errc::kInitFailed, "tideworn: wetsuit material failed");
  }
  diver_ = scene_.CreateNode(scene_.Root());
  diver_mesh_ = scene_.AddComponent<scene::SkinnedMeshComponent>(diver_);
  diver_mesh_->mesh = diver_model_->Meshes()[0].mesh;
  diver_mesh_->model = diver_model_;
  diver_mesh_->material = *wetsuit;
  diver_mesh_->casts_shadows = false;
  diver_mesh_->BuildStates();
  if (!diver_mesh_->Snap("idle")) {
    return Fail(Errc::kNotFound, "tideworn: the diver's idle clip");
  }
  // Out of every frustum until swim mode places it (same trick as the
  // first-person view uses to hide the body).
  scene_.SetLocalTransform(diver_,
                           Transform{.position = Vec3{0.0f, -1000.0f, 0.0f}});

  camera_ = scene_.CreateNode(scene_.Root());
  auto* view = scene_.AddComponent<scene::CameraComponent>(camera_);
  view->projection = ProjectionMode::kPerspective;
  view->reference_size = 0.0f;
  view->fov_y_radians = 0.72f;
  // Far enough for a horizon (the water mesh reaches 2.5 km); near rises with
  // it so a 24-bit depth buffer still resolves the water's thickness maths.
  view->near_z = 0.2f;
  view->far_z = 3000.0f;
  scene_.SetActiveCamera(camera_);
  orbit_ = scene_.AddComponent<scene::OrbitComponent>(camera_);
  orbit_->distance = options_.distance;
  orbit_->pitch = options_.pitch;
  orbit_->yaw = options_.yaw;
  orbit_->min_distance = 3.0f;
  orbit_->max_distance = 60.0f;

  // Pre-run the voyage (--start-clock): a swim capture wants daylight and a
  // settled school, and the clock only reaches them by having elapsed.
  for (F32 t = 0.0f; t < options_.start_clock; t += 1.0f / 60.0f) {
    world_.Step(runtime::LatchedInput{}, 1.0f / 60.0f);
  }
  world_.ClearEvents();

  if (options_.swim) {
    const runtime::ViewSnapshot at_start = world_.Snapshot();
    // Drop in beside the boat, at --depth, aimed by --pitch/--yaw. Those two
    // options drive the ORBIT rig otherwise; reusing them here rather than
    // adding swim-only twins keeps one pair of names for "where is the camera
    // looking", which is what a capture command is really saying.
    const Vec3 eye{at_start.boat_pos.x - 2.0f, -std::max(options_.depth, 0.0f),
                   at_start.boat_pos.y + 6.0f};
    // Same convention as SwimCamera::Forward: positive pitch looks DOWN.
    const F32 cos_p = std::cos(options_.pitch);
    const Vec3 forward{-cos_p * std::sin(options_.yaw),
                       -std::sin(options_.pitch),
                       -cos_p * std::cos(options_.yaw)};
    swim_ = true;
    orbit_->enabled = false;
    swim_camera_.Enter(eye, forward);
  }

  sun_ = scene_.AddComponent<scene::PunctualLightComponent>(
      scene_.CreateNode(scene_.Root()),
      scene::PunctualLightComponent::Directional(Vec3{1.0f, 0.97f, 0.92f}, 3.4f,
                                                 kSunDir));
  sun_->SetCastsShadows(true);
  // The lightning flash. Created AFTER the sun ON PURPOSE: the water derives
  // its sun from the FIRST directional light in the frame (ADR-0055), and a
  // bolt must not become the sea's sun for a frame.
  flash_ = scene_.AddComponent<scene::PunctualLightComponent>(
      scene_.CreateNode(scene_.Root()),
      scene::PunctualLightComponent::Directional(Vec3{0.78f, 0.85f, 1.0f}, 0.0f,
                                                 Vec3{0.0f, -1.0f, 0.0f}));
  flash_->SetCastsShadows(false);
  return {};
}

void TidewornGame::Update(const aether::app::AppContext& ctx, F32 dt) {
  // V swaps camera MODES (orbit = sailing, first-person = diving). The swim
  // camera takes over exactly where the orbit rig was looking, and the orbit
  // component is disabled so only one driver ever writes the node.
  if (ctx.input.JustPressed(platform::Key::kV) && orbit_ != nullptr) {
    swim_ = !swim_;
    orbit_->enabled = !swim_;
    if (swim_) {
      // The camera hangs off the root, so its LOCAL transform is its world
      // pose; the orbit rig always looks at its pivot.
      const Vec3 eye = scene_.Get(camera_)->local.position;
      swim_camera_.Enter(eye, Normalize(orbit_->pivot - eye));
    } else {
      // Back on deck: the body leaves the water and every frustum.
      scene_.SetLocalTransform(
          diver_, Transform{.position = Vec3{0.0f, -1000.0f, 0.0f}});
    }
  }
  if (swim_) {
    // F flips the swim view: third-person follow <-> first-person eyes.
    if (ctx.input.JustPressed(platform::Key::kF)) {
      swim_camera_.third_person = !swim_camera_.third_person;
    }
    // The capture autopilot's stroke: a steady glide keeps pace with the
    // boat so a hands-off capture still swims through the school.
    const F32 glide = ctx.Capturing() ? 1.2f : 0.0f;
    scene_.SetLocalTransform(camera_,
                             swim_camera_.Apply(ctx.input, ui_, dt, glide));
    scene_.SetLocalTransform(diver_, swim_camera_.BodyTransform());
    // Swim when moving, drift when not — a cross-fade, not a snap.
    if (diver_mesh_ != nullptr && swim_camera_.Moving() != diver_swimming_) {
      diver_swimming_ = swim_camera_.Moving();
      diver_mesh_->Machine().Request(
          StringId::FromRuntime(diver_swimming_ ? "swim" : "idle"));
    }
  } else if (orbit_ != nullptr) {
    orbit_input_.Apply(*orbit_, ctx.input, ui_);
  }
  // Runs the component updates — the OrbitComponent writes the camera's
  // transform here, so skipping this leaves the camera at the origin.
  scene_.Update(dt);
}

void TidewornGame::FixedUpdate(const aether::app::AppContext& /*ctx*/, F32 dt) {
  // The diver enters the sim as INPUT (the latch rule): the step stays a pure
  // function of (world, LatchedInput, dt), and a replay records the dive.
  runtime::LatchedInput input;
  if (swim_ && swim_camera_.Position().y < 0.0f) {
    input.diver_present = true;
    input.diver_xz = Vec2{swim_camera_.Position().x, swim_camera_.Position().z};
  }
  world_.Step(input, dt * options_.time_scale);
}

void TidewornGame::DrainEvents(F32 clock_s) {
  for (const runtime::GameEvent& event : world_.Events()) {
    if (const auto* band = std::get_if<voyage::BandChanged>(&event)) {
      // Presentation reaction; music hooks here in later phases.
      LogInfo("tideworn: weather {} -> {}", BandName(band->from),
              BandName(band->to));
    } else if (const auto* bolt =
                   std::get_if<runtime::LightningStruck>(&event)) {
      strikes_.emplace_back(clock_s, bolt->azimuth);
      // Thunder waits on an audio asset (F1's 3D audio is ready for it).
    }
  }
  world_.ClearEvents();
  std::erase_if(strikes_,
                [clock_s](const auto& s) { return clock_s - s.first > 1.0f; });
}

// THE DISTURBANCE GRID: rain pits at rate-matched random positions (see
// view::Rain — statistically identical to per-drop tracking), the wake as a
// steady stamp under the hull, all on the sim clock.
void TidewornGame::StepDisturbance(const runtime::ViewSnapshot& snapshot) {
  static constexpr F32 kRainStampRadius = 15.0f;
  const F32 grid_dt =
      last_grid_clock_ < 0.0f
          ? 0.0f
          : std::clamp(snapshot.clock_s - last_grid_clock_, 0.0f, 0.25f);
  last_grid_clock_ = snapshot.clock_s;
  if (grid_dt <= 0.0f) {
    return;
  }
  if (rain_.Raining()) {
    std::uniform_real_distribution<F32> unit(-kRainStampRadius,
                                             kRainStampRadius);
    splash_accum_ += rain_.Rate() * grid_dt;
    while (splash_accum_ >= 1.0f) {
      splash_accum_ -= 1.0f;
      disturbance_.Splash(Vec2{snapshot.boat_pos.x + unit(splash_rng_),
                               snapshot.boat_pos.y + unit(splash_rng_)},
                          0.06f);
    }
  }
  // The wake: a continuous stamp under the hull, scaled by way-on-speed.
  disturbance_.Splash(snapshot.boat_pos, 2.5f * grid_dt);
  disturbance_.Step(grid_dt, snapshot.boat_pos);
}

RenderFrame TidewornGame::Extract(const aether::app::AppContext& ctx) {
  const runtime::ViewSnapshot snapshot = world_.Snapshot();
  view::ApplySeaState(ctx.renderer, snapshot, kSeaLevel);
  DrainEvents(snapshot.clock_s);

  // The boat RIDES the sea it sails: the same oracle the shader draws.
  const render::WaterWaves waves = view::SeaWaves(snapshot);
  const render::WaveSample under =
      render::EvaluateSurfaceAt(waves, snapshot.boat_pos);
  const Vec3 boat_at{snapshot.boat_pos.x, kSeaLevel + under.height,
                     snapshot.boat_pos.y};
  scene_.SetLocalTransform(
      boat_, Transform{.position = boat_at,
                       .rotation = TiltTo(under.normal) *
                                   QuatFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f},
                                                     snapshot.boat_heading),
                       .scale = Vec3{2.6f, 0.8f, 1.2f}});
  creatures_.Sync(scene_, snapshot.fish);
  if (orbit_ != nullptr) {
    orbit_->pivot = boat_at + Vec3{0.0f, 0.8f, 0.0f};
  }

  spray_.Step(snapshot, waves, snapshot.boat_pos);
  rain_.Step(snapshot, waves.wind_direction, snapshot.boat_pos);

  StepDisturbance(snapshot);

  // THE SKY, before BuildRenderFrame: the sun light must carry this frame's
  // time of day when the extract walks the scene. Everything here derives
  // from the two neighbouring keyframes' manifests — no authored sun table.
  const view::SkyRing::State sky = sky_ring_.Evaluate(snapshot.time_of_day);
  if (sky.valid && sun_ != nullptr) {
    sun_->SetLocalDirection(sky.sun_travel);
    sun_->SetColor(sky.sun_color);
    sun_->SetIntensity(sky.sun_intensity);
    // A sub-horizon sun casts nothing worth a shadow atlas.
    sun_->SetCastsShadows(sky.sun_intensity > 0.05f);
  }

  // LIGHTNING FLASH: the sharpest recent strike's decay envelope, on the sim
  // clock. Drives the flash light before the extract walks the scene.
  F32 flash = 0.0f;
  F32 flash_azimuth = 0.0f;
  for (const auto& [at, azimuth] : strikes_) {
    const F32 age = snapshot.clock_s - at;
    if (age >= 0.0f) {
      const F32 energy = std::exp(-age * 16.0f);
      if (energy > flash) {
        flash = energy;
        flash_azimuth = azimuth;
      }
    }
  }
  if (flash_ != nullptr) {
    flash_->SetIntensity(15.0f * flash);
    flash_->SetLocalDirection(
        Normalize(Vec3{std::cos(flash_azimuth) * 0.55f, -1.0f,
                       std::sin(flash_azimuth) * 0.55f}));
  }

  RenderFrame frame = scene_.BuildRenderFrame(Viewport{
      .width = ctx.render_size.width, .height = ctx.render_size.height});
  // Ring slot written UNCONDITIONALLY per Extract (the recipe's rule), so a
  // sprayless frame clears its slot rather than replaying a stale one.
  FrameFx& fx = fx_ring_[extract_count_ % kFxRing];
  ++extract_count_;
  fx.spray.clear();
  // Below the surface the water pass becomes the fog/ceiling. Tested against
  // the WAVE at the eye, not the flat plane — a trough can bare the camera.
  // HYSTERESIS, because a camera bobbing at the waterline would otherwise
  // flip the whole frame's render path every few frames: submerge only past
  // 12 cm, resurface only once actually clear.
  const Vec3 cam = frame.view.camera.position;
  const render::WaveSample at_cam =
      render::EvaluateSurfaceAt(waves, Vec2{cam.x, cam.z});
  const F32 depth_below = (kSeaLevel + at_cam.height) - cam.y;
  was_underwater_ = was_underwater_ ? depth_below > 0.0f : depth_below > 0.12f;
  fx.underwater = was_underwater_;
  // Within 2 m of the wave surface the near plane (plus a crest's metre of
  // displacement) can straddle the waterline — arm the straddle fill. Generous
  // on purpose: the fill discards itself where the mesh covers the pixel.
  fx.near_surface = !was_underwater_ && depth_below > -2.0f;
  // One AIRBORNE list: spray and rain both live above the water and share the
  // post-water pass (journal 2026-08-16#12) — and neither exists underwater.
  if (!fx.underwater) {
    spray_.Emit(fx.spray, frame.view.camera);
    rain_.Emit(fx.spray, frame.view.camera);
  }
  disturbance_.EncodeGradient(fx.grid_pixels);
  fx.grid_region = disturbance_.Region();
  fx.sky_a = sky.valid ? sky.cube_a : TextureHandle{};
  fx.sky_b = sky.valid ? sky.cube_b : TextureHandle{};
  fx.sky_blend = sky.valid ? sky.blend : 0.0f;
  fx.sky_flash = flash;

  frame.environment.ambient = Vec3{0.34f, 0.42f, 0.52f};
  if (sky.valid) {
    frame.environment.specular = sky.cube_a;
    frame.environment.specular_b = sky.cube_b;
    frame.environment.specular_blend = sky.blend;
    frame.environment.specular_ladder_levels = sky.ladder_levels;
    frame.environment.irradiance = sky.irradiance;
    // The bolt lights the CLOUDS: a boost on the SH's DC term brightens the
    // whole ambient fill for the flash's few frames.
    frame.environment.irradiance[0] = frame.environment.irradiance[0] +
                                      Vec3{0.9f, 1.0f, 1.15f} * (0.5f * flash);
  } else if (environment_) {
    frame.environment.specular = environment_->Specular();
    frame.environment.specular_ladder_levels = environment_->LadderLevels();
    const auto sh = environment_->Irradiance();
    std::ranges::copy(sh, frame.environment.irradiance.begin());
  }
  return frame;
}

void TidewornGame::DrawSky(const aether::app::FrameContext& c) {
  c.target.Ensure(c.app.device, c.app.render_size,
                  rhi::TextureFormat::kRGBA16F);
  const Size target = c.app.device.TargetPixelSize(c.target.fb);
  const Viewport rect{
      .x = 0, .y = 0, .width = target.width, .height = target.height};
  // This frame's keyframe pair, from the ring slot Extract wrote (a pass must
  // not read live game state); the single-bake env is the fallback.
  const FrameFx& fx = fx_ring_[c.frame_index % kFxRing];
  TextureHandle cube = fx.sky_a;
  if (!cube.Valid() && environment_) {
    cube = environment_->Specular();
  }
  if (!cube.Valid() || !skybox_.Ready()) {
    c.app.device.BeginPass(rhi::Device::RenderPass{
        .target = c.target.fb,
        .viewport = rect,
        .clear_rgba = aether::app::LinearClear(c.clear_color),
        .clear = true,
        .depth = true});
    return;
  }
  const F32 aspect = target.height > 0 ? static_cast<F32>(target.width) /
                                             static_cast<F32>(target.height)
                                       : 1.0f;
  skybox_.Draw(c.app.device, c.target.fb, rect, cube, c.frame.view.camera,
               aspect,
               render::SkyboxParams{.intensity = 1.0f + 2.2f * fx.sky_flash,
                                    .environment_b = fx.sky_b,
                                    .blend = fx.sky_blend});
}

void TidewornGame::Unload(const aether::app::AppContext& ctx) {
  ctx.device.Destroy(disturbance_texture_);
  disturbance_texture_ = {};
  scene_copy_.Destroy(ctx.device);
  rain_.Unload(ctx.device);
  spray_.Unload(ctx.device);
  water_mesh_.Destroy(ctx.device);
  skybox_.Destroy(ctx.device);
  hull_ = {};
  environment_ = {};
}

}  // namespace tideworn::app
