// Whitecap spray: short-lived billboards thrown off breaking crests. PURE
// PRESENTATION — it probes the same CPU wave oracle the shader draws (foam is
// where the crest field crosses the Monahan threshold), so spray appears
// exactly where the surface whitens, with no new sim state.
#pragma once

#include <random>
#include <vector>

#include "aether/core/error.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/render/water.hpp"
#include "aether/resources/texture.hpp"
#include "tideworn/runtime/snapshot.hpp"

namespace aether::rhi {
class Device;
}

namespace tideworn::view {

class Spray {
 public:
  [[nodiscard]] aether::Result<void> Load(aether::rhi::Device& device);
  void Unload(aether::rhi::Device& device);

  // Advances on the SIM clock (snapshot.clock_s), never wall dt, so a
  // deterministic capture shows the same spray every run. `center` is where
  // the probes cluster — the boat, which is where the eye is.
  void Step(const runtime::ViewSnapshot& snapshot,
            const aether::render::WaterWaves& waves, aether::Vec2 center);

  // Camera-facing billboards appended to the frame's sprite items.
  void Emit(std::vector<aether::Renderable>& out,
            const aether::Camera& camera) const;

 private:
  // A fixed ring: the budget cap by construction — a storm OVERWRITES the
  // oldest spray rather than growing the frame.
  static constexpr aether::Usize kMax = 384;
  struct Particle {
    aether::Vec3 position{0.0f, 0.0f, 0.0f};
    aether::Vec3 velocity{0.0f, 0.0f, 0.0f};
    aether::F32 age = 0.0f;
    aether::F32 life = 0.0f;  // 0 = dead slot
    aether::F32 size = 0.0f;
  };
  std::vector<Particle> particles_{kMax};
  aether::Usize cursor_ = 0;
  aether::F32 last_clock_ = -1.0f;
  // Presentation-only randomness, FIXED seed: emission order is a pure
  // function of the sim clock, so captures reproduce.
  std::mt19937 rng_{1913};
  std::shared_ptr<const aether::resources::Texture> dot_;
};

}  // namespace tideworn::view
