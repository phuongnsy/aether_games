// Weather presentation: the textures and the per-frame Emit of the sim's
// fields into the render frame. PRESENTATION, deliberately — the sim decides
// where drops, splashes and snow ARE; this decides only what they look like.
#pragma once

#include <vector>

#include "aether/core/render_frame.hpp"
#include "aether/core/types.hpp"
#include "aether/resources/texture.hpp"
#include "aether/rhi/device.hpp"
#include "lantern/features/weather/weather.hpp"

namespace lantern::view {

class WeatherFx {
 public:
  // Build the streak/flake/cap textures. Call once in Load, BEFORE the sim's
  // Setup — the sim wants the opaque handles.
  aether::Result<void> Load(aether::rhi::Device& device);

  [[nodiscard]] aether::TextureHandle RainTexture() const;
  [[nodiscard]] aether::TextureHandle SnowTexture() const;

  // Append everything visible: streaks/flakes (the fields' own billboards),
  // snow caps on the platform grids, splash discs at the sim's impact points.
  void Emit(std::vector<aether::Renderable>& out, const aether::Camera& camera,
            const weather::WeatherSim& sim) const;

 private:
  aether::resources::ResourceHandle<aether::resources::Texture> streak_;
  aether::resources::ResourceHandle<aether::resources::Texture> dot_;
  aether::resources::ResourceHandle<aether::resources::Texture> white_;
};

}  // namespace lantern::view
