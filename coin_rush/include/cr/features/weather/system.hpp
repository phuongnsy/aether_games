// Weather feature — the sim half, registered FIRST so its wetness is fresh
// when movement reads it; the view Emits the fields (sim never renders).
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "aether/core/field.hpp"
#include "aether/core/gpu_handles.hpp"
#include "aether/core/types.hpp"
#include "aether/scene_core/geometry_query.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/surface/accumulation_field.hpp"
#include "aether/weather/rain_field.hpp"
#include "cr/content/levels.hpp"  // WeatherConfig
#include "cr/runtime/sim_system.hpp"
#include "cr/runtime/world_view.hpp"

namespace game {

using namespace aether;  // NOLINT(google-build-using-namespace)

class WeatherSystem final : public SimSystem {
 public:
  // Build the rain/snow producers + collision query ONCE (Load). The spawn band
  // is framed by the level extents (half_w = half map width, top = above it).
  void Setup(scene::Scene& scene, WorldView& world, TextureHandle particle_tex,
             U32 solid_mask, F32 half_w, F32 top);

  void SetConfig(const WeatherConfig& cfg) { cfg_ = cfg; }
  [[nodiscard]] const WeatherConfig& Config() const { return cfg_; }

  // (Re)register the fresh ground snow surface for a round: a straight top-edge
  // polyline from -half_width..+half_width at ground_top (world units).
  void RegisterSnowSurface(F32 half_width, F32 ground_top);

  void Step(StepContext& ctx, const EventList& in, EventList& out) override;

  // Read-only field access for the view's Emit (rendering stays in view; the
  // sim never renders). Rain()/Snow() are null until Setup runs.
  [[nodiscard]] const weather::RainField* Rain() const { return rain_.get(); }
  [[nodiscard]] const weather::RainField* Snow() const { return snow_.get(); }
  [[nodiscard]] const surface::AccumulationField& Accum() const {
    return accum_;
  }

 private:
  WeatherConfig cfg_;
  std::unique_ptr<weather::RainField> rain_;
  std::unique_ptr<weather::RainField> snow_;  // L2 falling snow (accumulates)
  surface::AccumulationField accum_;          // snow piled on the ground
  std::optional<scene::SceneGeometryQuery> geom_;
  ValueField<F32> sky_{1.0f};    // open sky (no rain shelter)
  ValueField<F32> cold_{-5.0f};  // sub-zero → snow never melts
  std::vector<weather::Impact> rain_impacts_;
  std::vector<weather::Impact> snow_impacts_;
};

}  // namespace game
