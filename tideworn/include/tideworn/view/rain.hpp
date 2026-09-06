// Storm rain over open water: aether::weather's 3D rain field, spawned over
// the boat, dying at the sea surface. Pure presentation — intensity follows
// the snapshot's weather band, and the field steps on the SIM clock so a
// replayed storm rains the same drops.
#pragma once

#include <optional>
#include <vector>

#include "aether/core/error.hpp"
#include "aether/core/field.hpp"
#include "aether/core/geometry_query.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/resources/texture.hpp"
#include "aether/weather/rain_field3.hpp"
#include "tideworn/runtime/snapshot.hpp"

namespace aether::rhi {
class Device;
}

namespace tideworn::view {

class Rain {
 public:
  [[nodiscard]] aether::Result<void> Load(aether::rhi::Device& device);
  void Unload(aether::rhi::Device& device);

  void Step(const runtime::ViewSnapshot& snapshot, aether::Vec2 wind_dir,
            aether::Vec2 center);
  // Streak billboards. Appended to the POST-water airborne list — the water
  // pass composites over the scene pass, so rain drawn there would vanish
  // under the sea it falls toward (the spray lesson, journal 2026-08-16#12).
  void Emit(std::vector<aether::Renderable>& out,
            const aether::Camera& camera) const;

  // For the disturbance grid's rain stamps: storm rain is statistically
  // uniform, so rate-matched RANDOM splash positions are visually identical
  // to per-drop impact tracking and cost no engine plumbing.
  [[nodiscard]] bool Raining() const;
  [[nodiscard]] aether::F32 Rate() const;

 private:
  std::optional<aether::weather::RainField3> field_;
  // The open sea: every ray misses, the whole sky is open. "No shelter" is a
  // world, not a special case (core's NullGeometryQuery3).
  aether::NullGeometryQuery3 open_sea_;
  aether::ValueField<aether::F32> open_sky_{1.0f};
  std::vector<aether::weather::Impact3> impacts_;  // future splash/foam input
  aether::F32 last_clock_ = -1.0f;
  std::shared_ptr<const aether::resources::Texture> streak_;
};

}  // namespace tideworn::view
