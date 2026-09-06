#include "hf/view/weather_fx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include "aether/core/error.hpp"
#include "aether/core/log.hpp"
#include "aether/core/math/transform.hpp"
#include "aether/render/camera.hpp"
#include "aether/scene_core/collider3_component.hpp"
#include "hf/content/farm.hpp"

namespace hearthfield::view {
namespace {

using namespace aether;

// Enough to read as weather over an 8 m board without becoming a curtain. Not
// tuned by measurement — nothing here is hot — but stated as a choice.
constexpr Usize kDropCapacity = 420;
constexpr F32 kSpawnRate = 260.0f;
// How fast the soil takes water and gives it back. Wetting is fast because a
// shower reads instantly; drying is slow because a farm staying damp after the
// rain is the point of having wetness at all rather than a rain toggle.
//
// MEASURED, not guessed. At 0.05 the board went from dry to saturated in five
// frames, which reads as a material swap rather than as weather: the spawn rate
// IS the impact rate, so 260 hits a second times anything visible saturates
// instantly. 0.0015 x 260 is ~0.4/s, so a shower soaks in over ~2.5 s and takes
// ~28 s to dry — the asymmetry is the point, since a farm staying damp after
// the rain is the only reason to have wetness rather than a rain toggle.
constexpr F32 kWetPerImpact = 0.0015f;
constexpr F32 kDryPerSecond = 0.035f;
// The spawn box overhangs the board so drops fall past its edges instead of
// stopping in a neat square. PROPORTIONAL, not a fixed 3 m: with a constant
// margin the fraction of drops that land on the board depends on its size, so
// a 2x2 farm soaked at a third the rate of an 8x8 one — the same rain falling
// differently on two boards under one sky.
constexpr F32 kSpawnOverhang = 1.35f;
constexpr F32 kSpawnHeight = 9.0f;

// --- the background layer ---------------------------------------------------
//
// EVERY NUMBER BELOW IS A FRACTION OF `ortho_half_height`, and that is forced
// rather than tidy. The zoom runs 3 to 45 — a 15x range — and under a parallel
// projection there is exactly one world-metres-per-pixel for the whole frame,
// so anything authored as a constant world size is right at one zoom and wrong
// at both ends. The gameplay layer already learned this the hard way: its
// comment records a 0.012 m streak that came to SEVEN TENTHS OF A PIXEL and
// rendered nothing at all while the simulation ran perfectly.
//
// One draw: 65,536 is exactly `kDrawInstanceChunk`, and the GPU baseline is
// flat from 10k to 100k (0.594 -> 0.583 ms), so this sits on the flat part.
constexpr U32 kSkyCapacity = 65536;
// Half-extent of the rain box, as a multiple of the half-height. The visible
// ground is `half_height * aspect` across and `half_height / sin(pitch)` deep;
// at 16:9 and a 35 degree pitch those are 1.78 and 1.74, so 2.0 covers both at
// any of the four yaw detents without computing either.
constexpr F32 kSkySpread = 2.0f;
// A drop is ~2 px wide and ~16 px tall at 720p. In world units 1 px is
// `2 * half_height / 720`, hence the 1/180 — and the stretch does the rest, so
// the sprite stays the same shape whatever the zoom.
constexpr F32 kSkyDropWidth = 1.0f / 180.0f;
constexpr F32 kSkyStretch = 8.0f;
// Fall speed likewise scales with the zoom, so rain crosses the SCREEN at one
// speed. Holding it constant in world units would be the physical answer and
// the wrong one here: this is a stylised parallel-projection farm where zoom is
// a UI convenience, not a camera move, and rain that blurs when you lean in
// reads as a bug. 20 is the authored default half-height, so 11 m/s is what it
// still is there.
constexpr F32 kSkyFallSpeed = 11.0f / 20.0f;
// Consequences of the two above, and they fall out constant: the box is
// `2 * half_height` tall and a drop crosses it in `2 * hh / (11 * hh / 20)`
// seconds no matter the zoom, so life and spawn rate need no scaling at all.
constexpr F32 kSkyLife = 40.0f / 11.0f;
// Under capacity/life on purpose: at exactly that rate the emitter runs against
// its own ceiling and starts dropping spawns, which shows up as a faint pulse.
constexpr F32 kSkySpawnRate = 0.92f * kSkyCapacity / kSkyLife;

// A cool, pale streak. ADDITIVE into a SCENE-LINEAR target, so this is a
// radiance rather than a colour picked against a resolved image — the
// gpu_particle_lab numbers do not carry over, because it composites after the
// tonemap against black.
//
// RAISED FROM 0.34/0.46/0.62 after looking at both zoom extremes, and the
// reason is CONTRAST rather than brightness: additive pale rain over flat sky
// is obvious, and over a busy mid-tone of grass, shadow and crop it disappears
// — the count was identical in both, 60,519 drops, so the first version read as
// "sparse rain" while being exactly as dense. Tuned against the worst
// background (the hub at the authored zoom), then re-checked at 45 to confirm
// it does not blow out over sky.
constexpr Vec4 kSkyColor{0.62f, 0.76f, 0.95f, 1.0f};

// A soft vertical streak. Shape lives in ALPHA only — rgb=value under straight
// src-alpha blending rims every drop with a dark halo, which is invisible
// against a dark sky and glaring against pale soil.
resources::ResourceHandle<resources::Texture> MakeStreak(rhi::Device& device) {
  constexpr U32 kW = 8;
  constexpr U32 kH = 32;
  std::array<U8, static_cast<Usize>(kW) * kH * 4> px{};
  for (U32 y = 0; y < kH; ++y) {
    const F32 fv = 1.0f - (static_cast<F32>(y) / (kH - 1));
    for (U32 x = 0; x < kW; ++x) {
      const F32 fu = ((static_cast<F32>(x) / (kW - 1)) * 2.0f) - 1.0f;
      F32 across = std::clamp(1.0f - (fu * fu), 0.0f, 1.0f);
      across *= across;
      const Usize i = ((static_cast<Usize>(y) * kW) + x) * 4;
      px[i] = px[i + 1] = px[i + 2] = 0xff;
      px[i + 3] = static_cast<U8>(std::clamp(across * fv, 0.0f, 1.0f) * 255.0f);
    }
  }
  auto gpu = device.CreateTexture(kW, kH, rhi::TextureFormat::kRGBA8, px.data(),
                                  static_cast<U32>(px.size()));
  return gpu ? std::make_shared<const resources::Texture>(device, *gpu, kW, kH)
             : nullptr;
}

}  // namespace

// Where the rain box goes for a given view. Centred on the point the camera is
// LOOKING AT rather than on the camera or on the world origin: the eye is
// hundreds of metres back by authoring accident under a parallel projection
// (ADR-0081), and the world origin is the hub, so rain would thin out as the
// player panned away from it.
WeatherFx::SkyVolume WeatherFx::VolumeFor(const Camera& camera) {
  const Vec3 forward =
      Normalize(Rotate(camera.rotation, Vec3{0.0f, 0.0f, -1.0f}));
  // Where the view axis crosses the ground plane. A camera looking level or up
  // has no such point, so the box just rides in front of the eye instead.
  const F32 reach = forward.y < -0.05f ? -camera.position.y / forward.y : 60.0f;
  const Vec3 focus = camera.position + (forward * reach);

  const F32 hh = std::max(camera.ortho_half_height, 0.01f);
  const F32 across = kSkySpread * hh;
  return SkyVolume{
      // Lifted by its own half-height so the box straddles the ground rather
      // than burying half its drops under the island.
      .centre = Vec3{focus.x, focus.y + hh, focus.z},
      .half_extent = Vec3{across, hh, across},
      .drop_size = kSkyDropWidth * hh,
      .fall_speed = kSkyFallSpeed * hh,
  };
}

Result<void> WeatherFx::Create(rhi::Device& device, scene::Scene& scene,
                               const runtime::Grid& grid) {
  streak_ = MakeStreak(device);
  if (!streak_) {
    return Fail(Errc::kInitFailed, "hearthfield: the rain texture failed");
  }

  // THE ONE SURFACE. A flat slab at the soil's top face, spanning the board.
  const F32 half = (0.5f * static_cast<F32>(grid.columns) * grid.cell) + 0.5f;
  const scene::NodeId ground = scene.CreateNode(scene.Root());
  auto* collider = scene.AddComponent<scene::Collider3Component>(ground);
  collider->kind = scene::Shape3Kind::kBox;
  collider->half_extents = Vec3{half, 0.05f, half};
  scene.SetLocalTransform(
      ground, Transform{.position = Vec3{0.0f, content::kTileHeight, 0.0f},
                        .rotation = Quat::Identity(),
                        .scale = Vec3{1.0f, 1.0f, 1.0f}});
  ground_surface_ = static_cast<U64>(ground.id);
  // SyncTransforms, NOT Update — the query snapshots WORLD proxies, and
  // `Scene::Update` does not compute them; it ticks components. This said
  // `Update` from H5 until H6, so the ground slab sat at y = 0 instead of at
  // the soil's top face. It WORKED, which is why nothing noticed: the board is
  // centred on the origin, so a 0.12 m error in a slab the drops fall onto from
  // 9 m up changes nothing you can see. The comment was the bug.
  scene.SyncTransforms();
  query_.emplace(scene);

  const F32 reach =
      0.5f * static_cast<F32>(grid.columns) * grid.cell * kSpawnOverhang;
  rain_.emplace(weather::RainConfig3{
      .spawn_rate = kSpawnRate,
      .capacity = kDropCapacity,
      .spawn_y = content::kTileHeight + kSpawnHeight,
      .spawn_min = Vec2{-reach, -reach},
      .spawn_max = Vec2{reach, reach},
      .shelter_probe_y = content::kTileHeight,
      // Nothing on a farm shelters anything, so a cull would only cost the
      // probe. Zero says that outright.
      .shelter_cull = 0.0f,
      .velocity_min = Vec3{-0.6f, -11.0f, -0.6f},
      .velocity_max = Vec3{-0.1f, -9.0f, -0.1f},
      .max_life = 4.0f,
      .kill_below_y = -1.0f,
      .texture = streak_->Handle(),
      // SIZED FOR A PARALLEL PROJECTION, and this is where lantern's numbers do
      // not carry over. The view is 12 m tall in 720 px, so lantern's 0.012 m
      // streak is SEVEN TENTHS OF A PIXEL — the field ran correctly, 135 drops
      // live and the soil soaking, and the screen showed nothing at all. Under
      // perspective a near drop is large and a far one small; here every drop
      // is the same size forever, so that size has to be chosen against the
      // frame rather than against the world.
      .width = 0.045f,
      .stretch = 0.10f,
      .color = Vec4{0.78f, 0.86f, 1.0f, 0.95f},
      // A band rather than a sheet: releasing every drop from one height reads
      // as a descending layer until the fall speeds separate them, and gives no
      // depth at all on the first frames.
      .spawn_height = 4.0f,
      .size_jitter = 0.3f,
      .alpha_jitter = 0.2f});
  rain_->SetEmitting(false);

  // THE BACKGROUND LAYER, and a failure here is not a failure of Create. No
  // compute means no emitter (WebGL2), and the game must still have its rain,
  // its impacts and its wetness — the same bargain FarmAudio::Load makes for a
  // missing clip. Said out loud rather than swallowed, because "the sky layer
  // is missing" and "the sky layer is off" look identical on screen.
  //
  // The geometry-shaped parameters are placeholders: SubmitBackground rewrites
  // origin, extent, size and velocity every frame from the camera, and only
  // `capacity` is fixed at creation.
  //
  // AETHER_NO_GPU_RAIN REACHES THE ABSENT CASE ON A MACHINE THAT HAS COMPUTE.
  // That case is the web build's, and "it will fall back" is exactly the class
  // of claim this repo has been burned by — so it is reachable rather than
  // reasoned about.
  if (std::getenv("AETHER_NO_GPU_RAIN") != nullptr) {
    LogWarn("hearthfield: background rain forced off (AETHER_NO_GPU_RAIN)");
    return {};
  }
  auto emitter = render::GpuParticleEmitter::Create(
      device, render::GpuParticleParams{
                  .capacity = kSkyCapacity,
                  .spawn_rate = kSkySpawnRate,
                  .life_min = kSkyLife * 0.85f,
                  .life_max = kSkyLife * 1.15f,
                  .color = kSkyColor,
                  // NO GRAVITY. Rain reaches terminal velocity in the first few
                  // metres, so accelerating it makes the top of the box drift
                  // and the bottom streak — the drops are already falling at
                  // the speed they fall at.
                  .gravity = Vec3{0.0f, 0.0f, 0.0f},
                  .drag = 0.0f,
                  .size_start_scale = 1.0f,
                  // No shrink: a drop does not get smaller as it falls, and the
                  // 0.2 default would taper every streak to nothing halfway
                  // down.
                  .size_end_scale = 1.0f,
                  // The two knobs r1 added, and the reason it added them: the
                  // emitter stores a random turn per particle, so a vertical
                  // streak sprite spun at random is SPARKS.
                  .rotation_jitter = 0.0f,
                  .stretch = kSkyStretch,
                  .texture = streak_->Handle()});
  if (emitter) {
    sky_ = std::move(*emitter);
    sky_->SetEmitting(false);
  } else {
    LogWarn(
        "hearthfield: no background rain ({}) — the colliding layer still runs",
        emitter.error().message);
  }
  return {};
}

void WeatherFx::Update(F32 dt, bool raining) {
  if (!rain_) {
    return;
  }
  rain_->SetEmitting(raining);
  impacts_.clear();
  // Stepped even when dry: the drops already in the air must finish falling,
  // and the soil must go on drying. Gating the whole thing on `raining` would
  // freeze a curtain of rain in the sky the instant the spell ended.
  rain_->Step(dt, *query_, open_sky_, impacts_);
  for (const weather::Impact3& hit : impacts_) {
    wetness_.Add(hit.surface, kWetPerImpact);
  }
  wetness_.Decay(dt, kDryPerSecond);

  // THE SKY LAYER IS GATED, and the gate is not an optimisation. The compute
  // dispatch covers the whole CAPACITY every frame whether one drop is alive or
  // none, so an emitter left stepping through a dry week is half a millisecond
  // of nothing, forever. Stepped while draining, though — cutting it dead would
  // freeze 60,000 drops in the sky the instant the shower ended.
  sky_raining_ = raining;
  if (sky_ != nullptr) {
    sky_->SetEmitting(raining);
    if (raining || sky_->LiveEstimate() > 0) {
      sky_->Step(dt);
    }
  }
}

void WeatherFx::SubmitBackground(rhi::Device& device, const RenderView& view,
                                 FramebufferHandle target) {
  if (sky_ == nullptr || (!sky_raining_ && sky_->LiveEstimate() == 0)) {
    return;
  }
  const SkyVolume volume = VolumeFor(view.camera);
  render::GpuParticleParams params = sky_->Params();
  params.origin = volume.centre;
  params.spawn_extent = volume.half_extent;
  // A slight lean, so the rain has a direction and does not read as a static
  // curtain of vertical lines. Small: the streaks are billboarded to the screen
  // and a steep slant would show them as the flat cards they are.
  params.initial_velocity = Vec3{-0.12f * volume.fall_speed, -volume.fall_speed,
                                 -0.08f * volume.fall_speed};
  params.velocity_jitter = 0.10f * volume.fall_speed;
  params.size_min = volume.drop_size * 0.75f;
  params.size_max = volume.drop_size * 1.35f;
  sky_->SetParams(params);

  // ITS OWN PASS, with `clear` FALSE. That is what keeps the colour AND the
  // depth the scene pass just wrote, so the draw's depth-test-only state hides
  // a drop behind an island for free. Submitting without opening one lands the
  // draw in the previous pass's view — engine/render/CLAUDE.md records that
  // costing a debugging round, because the transform there is identity and
  // every particle collapses to a single clip-space point.
  //
  // `depth = false` DOES NOT MEAN NO DEPTH TEST, which is worth stating because
  // it reads exactly like it does. The field is the pass-wide default; a draw
  // whose own state is `kTestOnly` tests regardless (device_bgfx.cpp), and
  // writing is what must not happen for a translucent billboard. PROVED rather
  // than assumed 2026-08-29, by moving the box under the island: the rain
  // vanished behind it and only the drops over the void survived.
  const render::ViewProjection vp =
      render::ComputeViewProjection(view, device.HomogeneousDepth());
  const Size px = device.TargetPixelSize(target);
  device.BeginPass(rhi::Device::RenderPass{
      .target = target,
      .viewport =
          Viewport{.x = 0, .y = 0, .width = px.width, .height = px.height},
      .clear = false,
      .depth = false,
      .debug_name = "hearthfield_rain"});
  sky_->Submit(device, vp.view, vp.projection);
}

U32 WeatherFx::SkyDropCount() const {
  return sky_ != nullptr ? sky_->LiveEstimate() : 0;
}

F32 WeatherFx::Wetness() const { return wetness_.Wetness(ground_surface_); }

void WeatherFx::Emit(std::vector<Renderable>& out, const Camera& camera) const {
  if (rain_) {
    rain_->Emit(out, camera);
  }
}

Usize WeatherFx::DropCount() const { return rain_ ? rain_->LiveCount() : 0; }

}  // namespace hearthfield::view
