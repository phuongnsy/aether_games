#include "cr/features/weather/system.hpp"

#include <array>
#include <span>
#include <utility>

#include "aether/core/math/vec.hpp"
#include "aether/surface/surface_material.hpp"
#include "cr/features/weather/tuning.hpp"

namespace game {

void WeatherSystem::Setup(scene::Scene& scene, WorldView& world,
                          TextureHandle particle_tex, U32 solid_mask,
                          F32 half_w, F32 top) {
  rain_ = std::make_unique<weather::RainField>(weather::RainConfig{
      .spawn_rate = 0.0f,
      .capacity = 520,
      .spawn_y = top,
      .spawn_x_min = -half_w,
      .spawn_x_max = half_w,
      .shelter_cull = 0.0f,
      .collision_mask = solid_mask,
      .gravity = Vec2{0.0f, -540.0f},
      .velocity_min = Vec2{-30.0f, -360.0f},
      .velocity_max = Vec2{30.0f, -280.0f},
      .texture = particle_tex,
      .width = 5.0f,
      .stretch = 0.05f,
      .color = Vec4{0.72f, 0.80f, 0.96f, 0.80f},
      .layer = 120,
      .blend = BlendClass::kTranslucent,
  });
  rain_->SetWindField(&world.WindField());
  rain_->SetWindResponse(kRainWindResp);
  snow_ = std::make_unique<weather::RainField>(weather::RainConfig{
      .spawn_rate = 0.0f,
      .capacity = 1400,
      .spawn_y = top,
      .spawn_x_min = -half_w,
      .spawn_x_max = half_w,
      .shelter_cull = 0.0f,
      .collision_mask = solid_mask,
      .gravity = Vec2{0.0f, -70.0f},
      .velocity_min = Vec2{-16.0f, -120.0f},
      .velocity_max = Vec2{16.0f, -90.0f},
      .max_life = 8.0f,
      .texture = particle_tex,
      .width = 4.0f,
      .stretch = 0.0f,
      .color = Vec4{0.92f, 0.95f, 1.0f, 0.85f},
      .layer = 118,
      .blend = BlendClass::kTranslucent,
  });
  snow_->SetWindField(&world.WindField());
  snow_->SetWindResponse(kSnowWindResp);
  geom_.emplace(scene);
  world.Env().SetWind(world.WindField());  // movement reads wind via Env
}

void WeatherSystem::RegisterSnowSurface(F32 half_width, F32 ground_top) {
  const std::array<Vec2, 2> edge = {Vec2{-half_width, ground_top},
                                    Vec2{half_width, ground_top}};
  surface::SurfaceMaterial mat;
  mat.Set(surface::SnowResponse{.max_thickness = kSnowThickness,
                                .fill_per_hit = kSnowFillPerHit,
                                .melt_rate = 0.0f,
                                .upness_min = 0.5f,
                                .color = Vec4{0.95f, 0.97f, 1.0f, 1.0f}});
  accum_.Register(kSnowSurface, std::span<const Vec2>(edge), 96,
                  std::move(mat));
}

// Author the wind, step the colliding rain (wets platforms) + falling snow, all
// writing the shared WorldView state movement reads next (weather runs first).
void WeatherSystem::Step(StepContext& ctx, const EventList& /*in*/,
                         EventList& /*out*/) {
  if (!rain_ || !snow_ || !geom_) {
    return;
  }
  geom_->Refresh();  // re-snapshot the platform colliders for the query
  auto& wind = ctx.world.WindField();
  wind.SetBase(Vec2{cfg_.wind_base, 0.0f});
  wind.SetGustStrength(cfg_.gust);
  wind.Advance(ctx.dt);

  rain_->SetSpawnRate(cfg_.rain_rate);
  rain_impacts_.clear();
  rain_->Step(ctx.dt, *geom_, sky_, rain_impacts_);
  auto& wetness = ctx.world.Wetness();
  for (const weather::Impact& im : rain_impacts_) {  // rain wets the platform
    wetness.Add(im.surface, kWetPerHit);
  }
  wetness.Decay(ctx.dt, kWetDryRate);

  // Snow (L2 Whiteout): flakes fall + collide + pile on the ground surface.
  snow_->SetSpawnRate(cfg_.snow ? kSnowRate : 0.0f);
  snow_impacts_.clear();
  snow_->Step(ctx.dt, *geom_, sky_, snow_impacts_);
  for (const weather::Impact& im : snow_impacts_) {
    accum_.Deposit(surface::SurfaceHit{
        .surface = kSnowSurface, .point = im.point, .normal = im.normal});
  }
  accum_.Update(ctx.dt, cold_);
}

}  // namespace game
