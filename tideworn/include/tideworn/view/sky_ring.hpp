// The time-of-day sky: eight baked keyframes of one physical ocean sky,
// crossfaded. Everything downstream — the IBL cubes, the SH fill, the
// directional sun — derives from the two neighbouring keyframes' manifests, so
// there is exactly one authored input (the ring) and nothing to keep in step.
#pragma once

#include <array>
#include <vector>

#include "aether/core/gpu_handles.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/resources/environment.hpp"

namespace tideworn::view {

class SkyRing {
 public:
  struct Key {
    aether::resources::ResourceHandle<aether::resources::Environment> env;
    aether::F32 at = 0.0f;  // day fraction this keyframe represents
  };

  struct State {
    bool valid = false;
    aether::TextureHandle cube_a;
    aether::TextureHandle cube_b;
    aether::F32 blend = 0.0f;
    aether::U32 ladder_levels = 0;
    std::array<aether::Vec3, 9> irradiance{};  // CPU-lerped SH
    // The sun the two manifests derived, blended: travel direction (what a
    // directional light wants), colour, and an intensity scaled from the
    // keyframes' measured sun irradiance — dusk dims and night goes out
    // because the BAKE says so, not because a curve was authored.
    aether::Vec3 sun_travel{0.0f, -1.0f, 0.0f};
    aether::Vec3 sun_color{1.0f, 1.0f, 1.0f};
    aether::F32 sun_intensity = 0.0f;
  };

  void SetKeys(std::vector<Key> keys) { keys_ = std::move(keys); }
  [[nodiscard]] bool Ready() const { return keys_.size() >= 2; }
  [[nodiscard]] State Evaluate(aether::F32 time_of_day) const;

 private:
  std::vector<Key> keys_;  // sorted by `at`, wrapping past the last
};

}  // namespace tideworn::view
