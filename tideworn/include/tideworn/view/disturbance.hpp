// The local disturbance grid: a verlet heightfield window following the boat,
// stamped by rain impacts and the wake, its gradient handed to the water
// shader (Renderer::SetWaterDisturbance). This is the ripple pool's recorded
// replacement — 8 analytic slots cannot carry a storm's rain — and it lives
// GAME-side on purpose: the engine owns the data path, the sim is policy
// until a second consumer graduates it.
#pragma once

#include <vector>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace tideworn::view {

class Disturbance {
 public:
  static constexpr aether::U32 kResolution = 128;
  static constexpr aether::F32 kExtent = 36.0f;  // metres, square window
  // Gradient decode scale for the RGBA8 encoding: texel = grad/kDecode + 0.5.
  // 1.0, not larger: 8-bit texels give LSB = kDecode/255 of slope, and a rain
  // ring is a few hundredths — a generous range quantizes it to nothing.
  static constexpr aether::F32 kDecode = 1.0f;

  void Splash(aether::Vec2 world_xz, aether::F32 amount);
  // Verlet wave step on the SIM clock; recentres the window on `center` in
  // whole cells so the field itself never swims.
  void Step(aether::F32 dt, aether::Vec2 center);
  // Encodes +grad(h) into RGBA8 (rg), ready for Device::UpdateTexture.
  void EncodeGradient(std::vector<aether::U8>& out) const;
  // {origin.x, origin.z, 1/extent, decode} — u_waterGrid's exact layout.
  [[nodiscard]] aether::Vec4 Region() const;
  [[nodiscard]] aether::F32 HeightAt(aether::Vec2 world_xz) const;

 private:
  [[nodiscard]] static aether::Usize Index(aether::U32 x, aether::U32 y) {
    return static_cast<aether::Usize>(y) * kResolution + x;
  }
  void ShiftCells(int dx, int dy);

  static constexpr aether::Usize kCells =
      static_cast<aether::Usize>(kResolution) * kResolution;
  std::vector<aether::F32> height_ = std::vector<aether::F32>(kCells, 0.0f);
  std::vector<aether::F32> prev_ = std::vector<aether::F32>(kCells, 0.0f);
  std::vector<aether::F32> scratch_ = std::vector<aether::F32>(kCells, 0.0f);
  aether::Vec2 origin_{-kExtent * 0.5f, -kExtent * 0.5f};  // min corner
};

}  // namespace tideworn::view
