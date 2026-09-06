#include "lantern/features/weather/weather.hpp"

#include <algorithm>

#include "aether/surface/surface_material.hpp"
#include "lantern/content/spire.hpp"

namespace lantern::weather {

namespace {

// The spawn rect frames the 16x16 ground slab with a metre of margin, high
// enough to clear the tallest tier (top 5.2 m) and the lanterns above it.
constexpr F32 kSpawnY = 10.0f;

// BOTH producers use one box that follows the view. Rain was world-fixed and
// smaller until 2026-08-14, which left it hanging over the spire instead of
// surrounding the player — and the reason given for the asymmetry (that rain is
// sheltered, so its box belongs to the world) was wrong about its own
// mechanism: the shelter probe samples the sky per COLUMN, wherever the box is.
constexpr F32 kSpawnHalf = 15.0f;
// Scaled with the area: the box grew from 18x18 to 30x30, so a rate that kept
// the old density would have thinned the storm by two thirds.
constexpr F32 kRainRate = 1900.0f;
constexpr F32 kSnowRate = 520.0f;

surface::SurfaceMaterial PlatformSnow() {
  surface::SurfaceMaterial m;
  // Persist while cold (cold_ is -5); fill sized so a platform whitens over
  // tens of seconds of ambience rather than instantly.
  m.Set(surface::SnowResponse{.max_thickness = 0.18f,
                              .fill_per_hit = 0.03f,
                              .melt_rate = 0.001f,
                              .upness_min = 0.6f,
                              .steep_falloff = 1.5f});
  m.Set(surface::ThermalResponse{.melt_per_degree = 0.004f});
  return m;
}

}  // namespace

void WeatherSim::Setup(scene::Scene& scene, Mode mode, U64 seed,
                       TextureHandle rain_tex, TextureHandle snow_tex) {
  query_.emplace(scene);
  sky_.emplace(*query_, 0xFFFFFFFFu);

  aether::weather::RainConfig3 rain_cfg;
  rain_cfg.spawn_rate = 0.0f;  // SetMode opens the tap
  rain_cfg.capacity = 3200;
  rain_cfg.spawn_y = kSpawnY;
  rain_cfg.spawn_min = Vec2{-kSpawnHalf, -kSpawnHalf};
  rain_cfg.spawn_max = Vec2{kSpawnHalf, kSpawnHalf};
  // A BAND: with a box that slides under the camera, drops released at one
  // height form a visible curtain along its trailing edge.
  rain_cfg.spawn_height = 10.0f;
  rain_cfg.shelter_probe_y = 0.2f;
  rain_cfg.velocity_min = Vec3{-0.5f, -14.0f, -0.5f};
  rain_cfg.velocity_max = Vec3{0.5f, -12.0f, 0.5f};
  rain_cfg.gravity = Vec3{0.0f, -6.0f, 0.0f};
  rain_cfg.kill_below_y = content::kFallY;
  rain_cfg.width = 0.015f;
  rain_cfg.texture = rain_tex;
  rain_cfg.color = Vec4{0.62f, 0.70f, 0.90f, 0.55f};
  rain_cfg.seed = seed;
  rain_.emplace(rain_cfg);

  aether::weather::RainConfig3 snow_cfg;
  snow_cfg.spawn_rate = 0.0f;
  snow_cfg.capacity = 6000;
  snow_cfg.spawn_y = 12.0f;
  snow_cfg.spawn_min = Vec2{-kSpawnHalf, -kSpawnHalf};
  snow_cfg.spawn_max = Vec2{kSpawnHalf, kSpawnHalf};
  snow_cfg.spawn_height = 12.0f;
  snow_cfg.shelter_probe_y = 0.2f;
  // Terminal velocity, no gravity: snow is already balanced against drag, and
  // accelerating it turns a long-lived flake into rain by the end of its life.
  snow_cfg.velocity_min = Vec3{-0.12f, -1.4f, -0.12f};
  snow_cfg.velocity_max = Vec3{0.12f, -0.6f, 0.12f};
  snow_cfg.gravity = Vec3{0.0f, 0.0f, 0.0f};
  snow_cfg.max_life = 12.0f;
  snow_cfg.kill_below_y = content::kFallY;
  // Sized so a flake covers a few pixels at play distance: a sub-pixel quad
  // aliases in and out as it drifts, which reads as static rather than snow.
  snow_cfg.width = 0.09f;
  snow_cfg.texture = snow_tex;
  snow_cfg.stretch = 0.0f;  // 0 selects the camera-facing billboard path
  snow_cfg.color = Vec4{0.92f, 0.94f, 1.0f, 0.85f};
  snow_cfg.seed = seed ^ 0x5eedu;
  // A volume of individually-sized, individually-drifting flakes.
  snow_cfg.size_jitter = 0.7f;
  snow_cfg.alpha_jitter = 0.55f;
  snow_cfg.sway_acceleration = 0.25f;
  snow_cfg.sway_frequency = 0.9f;
  snow_cfg.fade_begin = 20.0f;
  snow_cfg.fade_end = 48.0f;
  snow_.emplace(snow_cfg);

  SetMode(mode);
}

void WeatherSim::RegisterPlatform(U64 surface, Vec3 origin, Vec3 axis_u,
                                  Vec3 axis_v, U32 nu, U32 nv) {
  accum_.Register(surface, origin, axis_u, axis_v, nu, nv, PlatformSnow());
}

void WeatherSim::SetMode(Mode mode) {
  mode_ = mode;
  if (rain_) {
    rain_->SetSpawnRate(mode == Mode::kRain ? kRainRate : 0.0f);
  }
  if (snow_) {
    snow_->SetSpawnRate(mode == Mode::kSnow ? kSnowRate : 0.0f);
  }
}

void WeatherSim::SetViewCenter(Vec2 center_xz) {
  if (rain_) {
    rain_->SetSpawnCenter(center_xz);
  }
  if (snow_) {
    snow_->SetSpawnCenter(center_xz);
  }
}

void WeatherSim::Step(F32 dt) {
  if (!query_ || !rain_ || !snow_) {
    return;
  }
  query_->Refresh();

  impacts_.clear();
  rain_->Step(dt, *query_, *sky_, impacts_);
  for (const aether::weather::Impact3& im : impacts_) {
    wetness_.Add(im.surface, surface::WetnessResponse{}.add_per_hit);
    if (splashes_.size() < 512) {
      splashes_.push_back(Splash{.position = im.point, .normal = im.normal});
    }
  }

  impacts_.clear();
  snow_->Step(dt, *query_, *sky_, impacts_);
  for (const aether::weather::Impact3& im : impacts_) {
    accum_.Deposit(surface::SurfaceHit3{
        .surface = im.surface, .point = im.point, .normal = im.normal});
  }

  for (Splash& s : splashes_) {
    s.age += dt;
  }
  std::erase_if(splashes_,
                [](const Splash& s) { return s.age >= kSplashLife; });
  accum_.Update(dt, cold_);
  wetness_.Decay(dt);
}

}  // namespace lantern::weather
