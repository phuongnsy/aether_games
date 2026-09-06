#include "tideworn/view/spray.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "aether/core/sort_key.hpp"
#include "aether/rhi/device.hpp"

namespace tideworn::view {

using namespace aether;

namespace {

// Above the scene's translucents, below any future HUD layers.
constexpr U16 kSprayLayer = 22;
// Probes clustered where the eye is; beyond this spray is sub-pixel anyway.
constexpr F32 kProbeRadius = 45.0f;
constexpr int kProbes = 56;
// Particles per breaking probe. Storm coverage is a few per cent of the disc,
// so the arithmetic lands at roughly 100-300 alive in a storm — inside the
// ring, and zero in calm by the band gate below.
constexpr int kBurst = 3;
// Emission gate on the ORACLE's foam — the same threshold family the shader
// whitens at, so spray and whitecaps agree by construction.
constexpr F32 kFoamGate = 0.55f;

// A Gaussian dot whose skirt reaches exactly zero at the quad edge — lantern's
// weather dot, carried as this game's own copy (a game owns its assets).
resources::ResourceHandle<resources::Texture> MakeDot(rhi::Device& device) {
  constexpr U32 kSize = 32;
  std::array<U8, static_cast<Usize>(kSize) * kSize * 4> px{};
  const F32 c = (kSize - 1) * 0.5f;
  constexpr F32 kSigma = 0.36f;
  const F32 edge = std::exp(-1.0f / (2.0f * kSigma * kSigma));
  for (U32 y = 0; y < kSize; ++y) {
    for (U32 x = 0; x < kSize; ++x) {
      const F32 dx = (static_cast<F32>(x) - c) / c;
      const F32 dy = (static_cast<F32>(y) - c) / c;
      const F32 g = std::exp(-(dx * dx + dy * dy) / (2.0f * kSigma * kSigma));
      const F32 a = std::clamp((g - edge) / (1.0f - edge), 0.0f, 1.0f);
      const Usize i = (static_cast<Usize>(y) * kSize + x) * 4;
      px[i] = px[i + 1] = px[i + 2] = 0xff;
      px[i + 3] = static_cast<U8>(a * 255.0f);
    }
  }
  auto gpu = device.CreateTexture(kSize, kSize, rhi::TextureFormat::kRGBA8,
                                  px.data(), static_cast<U32>(px.size()));
  return gpu ? std::make_shared<const resources::Texture>(device, *gpu, kSize,
                                                          kSize)
             : nullptr;
}

}  // namespace

Result<void> Spray::Load(rhi::Device& device) {
  dot_ = MakeDot(device);
  if (!dot_) {
    return Fail(Errc::kInitFailed, "spray texture create failed");
  }
  return {};
}

void Spray::Unload(rhi::Device& /*device*/) { dot_.reset(); }

void Spray::Step(const runtime::ViewSnapshot& snapshot,
                 const render::WaterWaves& waves, Vec2 center) {
  const F32 clock = snapshot.clock_s;
  const F32 dt =
      last_clock_ < 0.0f ? 0.0f : std::clamp(clock - last_clock_, 0.0f, 0.25f);
  last_clock_ = clock;
  if (dt <= 0.0f) {
    return;
  }

  // Integrate the living. Ballistic with a light wind drift; life is the
  // kill switch (the drop visually re-enters the sea before it expires).
  for (Particle& p : particles_) {
    if (p.life <= 0.0f) {
      continue;
    }
    p.age += dt;
    if (p.age >= p.life) {
      p.life = 0.0f;
      continue;
    }
    p.velocity.y -= 9.81f * dt;
    p.position = p.position + p.velocity * dt;
  }

  // Spray is a whitecap phenomenon: a calm sea does not break, so the probes
  // are not worth their microseconds. The BAND, not a wind threshold: view
  // reads the snapshot's conclusions, never a feature's tuning (law 2).
  if (snapshot.band == voyage::Band::kCalm) {
    return;
  }
  std::uniform_real_distribution<F32> unit(0.0f, 1.0f);
  const Vec2 wind = Normalize(waves.wind_direction);
  for (int i = 0; i < kProbes; ++i) {
    // Fresh jittered positions every frame: a fixed probe lattice strobes as
    // crests sweep through it.
    const F32 r = kProbeRadius * std::sqrt(unit(rng_));
    const F32 theta = 6.2831853f * unit(rng_);
    const Vec2 at{center.x + r * std::cos(theta),
                  center.y + r * std::sin(theta)};
    const render::WaveSample sample = render::EvaluateWaves(waves, at);
    if (sample.foam < kFoamGate) {
      continue;
    }
    for (int b = 0; b < kBurst; ++b) {
      Particle& p = particles_[cursor_];
      cursor_ = (cursor_ + 1) % kMax;
      const F32 u = unit(rng_);
      p.position = Vec3{at.x + (unit(rng_) - 0.5f), sample.height + 0.35f,
                        at.y + (unit(rng_) - 0.5f)};
      // Thrown downwind off the crest, the way the wind strips a breaking
      // top. LOW and SHORT: spray hugs the sea — a taller arc with a longer
      // life read as snow rising into the sky, not water torn off a wave.
      p.velocity = Vec3{wind.x, 0.0f, wind.y} *
                       (0.4f * snapshot.wind_speed * (0.6f + 0.4f * u)) +
                   Vec3{0.0f, 0.9f + 1.4f * unit(rng_), 0.0f};
      p.age = 0.0f;
      p.life = 0.45f + 0.5f * unit(rng_);
      p.size = 0.4f + 0.5f * unit(rng_);
    }
  }
}

void Spray::Emit(std::vector<Renderable>& out, const Camera& camera) const {
  if (!dot_) {
    return;
  }
  const Vec3 right = Rotate(camera.rotation, Vec3{1.0f, 0.0f, 0.0f});
  const Vec3 up = Rotate(camera.rotation, Vec3{0.0f, 1.0f, 0.0f});
  const Vec3 fwd = Rotate(camera.rotation, Vec3{0.0f, 0.0f, -1.0f});
  const U64 key =
      MakeSortKey(BlendClass::kTranslucent, kSprayLayer, dot_->Handle().id, 0);
  for (const Particle& p : particles_) {
    if (p.life <= 0.0f) {
      continue;
    }
    const F32 t = p.age / p.life;
    // Grows as it disperses, fades as it thins — mist, not confetti.
    const F32 d = p.size * (0.7f + 0.9f * t);
    Mat4 world = Mat4::Identity();
    world.At(0, 0) = right.x * d;
    world.At(1, 0) = right.y * d;
    world.At(2, 0) = right.z * d;
    world.At(0, 1) = up.x * d;
    world.At(1, 1) = up.y * d;
    world.At(2, 1) = up.z * d;
    world.At(0, 2) = fwd.x;
    world.At(1, 2) = fwd.y;
    world.At(2, 2) = fwd.z;
    world.At(0, 3) = p.position.x;
    world.At(1, 3) = p.position.y;
    world.At(2, 3) = p.position.z;
    out.push_back(Renderable{
        .world = world,
        .size = Vec2{1.0f, 1.0f},
        .uv = Rect{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f},
        .color = Vec4{0.88f, 0.93f, 1.0f, 0.55f * (1.0f - t)},
        .texture = dot_->Handle(),
        .sort_key = key});
  }
}

}  // namespace tideworn::view
