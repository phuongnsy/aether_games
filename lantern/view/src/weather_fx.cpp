#include "lantern/view/weather_fx.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "aether/core/error.hpp"
#include "aether/core/sort_key.hpp"

namespace lantern::view {

using namespace aether;

namespace {

// Sprite layers above the 3D scene's translucents; caps under discs so a
// splash reads on top of settled snow.
constexpr U16 kCapLayer = 21;
constexpr U16 kSplashLayer = 22;

// A soft vertical streak (weather3d_lab's texture, and worldsim_explorer's
// before it — a game owns its assets, so it carries its own copy).
resources::ResourceHandle<resources::Texture> MakeStreak(rhi::Device& device) {
  constexpr U32 kW = 16;
  constexpr U32 kH = 64;
  std::array<U8, static_cast<Usize>(kW) * kH * 4> px{};
  for (U32 y = 0; y < kH; ++y) {
    const F32 fv = 1.0f - static_cast<F32>(y) / (kH - 1);
    const F32 dome = 1.0f - std::clamp((fv - 0.85f) / 0.15f, 0.0f, 1.0f);
    const F32 half_w = std::max(0.06f, 0.5f * dome);
    for (U32 x = 0; x < kW; ++x) {
      const F32 fu = (static_cast<F32>(x) / (kW - 1)) * 2.0f - 1.0f;
      const F32 side = fu / half_w;
      F32 wa = std::clamp(1.0f - side * side, 0.0f, 1.0f);
      wa *= wa;
      const Usize i = (static_cast<Usize>(y) * kW + x) * 4;
      px[i] = px[i + 1] = px[i + 2] = 0xff;
      px[i + 3] = static_cast<U8>(std::clamp(wa * fv, 0.0f, 1.0f) * 255.0f);
    }
  }
  auto gpu = device.CreateTexture(kW, kH, rhi::TextureFormat::kRGBA8, px.data(),
                                  static_cast<U32>(px.size()));
  return gpu ? std::make_shared<const resources::Texture>(device, *gpu, kW, kH)
             : nullptr;
}

// A soft radial dot for flakes and splash discs.
resources::ResourceHandle<resources::Texture> MakeDot(rhi::Device& device) {
  // 32px, not 16: a near flake covers a couple of hundred screen pixels, and a
  // 16px source visibly blocks up when stretched that far.
  constexpr U32 kSize = 32;
  std::array<U8, static_cast<Usize>(kSize) * kSize * 4> px{};
  const F32 c = (kSize - 1) * 0.5f;
  // A GAUSSIAN skirt with a bright core, offset so it reaches EXACTLY zero at
  // the edge — a linear falloff has a visible rim where it meets the quad, and
  // that rim is what makes a big soft flake read as a disc instead of a glow.
  constexpr F32 kSigma = 0.36f;
  const F32 edge = std::exp(-1.0f / (2.0f * kSigma * kSigma));
  for (U32 y = 0; y < kSize; ++y) {
    for (U32 x = 0; x < kSize; ++x) {
      const F32 dx = (static_cast<F32>(x) - c) / c;
      const F32 dy = (static_cast<F32>(y) - c) / c;
      const F32 r2 = dx * dx + dy * dy;
      const F32 g = std::exp(-r2 / (2.0f * kSigma * kSigma));
      const F32 a = std::clamp((g - edge) / (1.0f - edge), 0.0f, 1.0f);
      // Shape lives in ALPHA ONLY (rgb stays white, like the streak): rgb=v
      // under straight src-alpha blending rims every dot with a dark halo,
      // invisible on a dark sky and glaring against settled snow.
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

resources::ResourceHandle<resources::Texture> MakeWhite(rhi::Device& device) {
  constexpr std::array<U8, 4> px = {255, 255, 255, 255};
  auto gpu = device.CreateTexture(1, 1, rhi::TextureFormat::kRGBA8, px.data(),
                                  static_cast<U32>(px.size()));
  return gpu ? std::make_shared<const resources::Texture>(device, *gpu, 1, 1)
             : nullptr;
}

}  // namespace

Result<void> WeatherFx::Load(rhi::Device& device) {
  streak_ = MakeStreak(device);
  dot_ = MakeDot(device);
  white_ = MakeWhite(device);
  if (!streak_ || !dot_ || !white_) {
    return Fail(Errc::kInitFailed, "weather texture create failed");
  }
  return {};
}

TextureHandle WeatherFx::RainTexture() const { return streak_->Handle(); }
TextureHandle WeatherFx::SnowTexture() const { return dot_->Handle(); }

void WeatherFx::Emit(std::vector<Renderable>& out, const Camera& camera,
                     const weather::WeatherSim& sim) const {
  if (const auto* rain = sim.Rain()) {
    rain->Emit(out, camera);
  }
  if (const auto* snow = sim.Snow()) {
    snow->Emit(out, camera);
  }
  sim.Accum().Emit(out, white_->Handle(), kCapLayer);

  const U64 key =
      MakeSortKey(BlendClass::kTranslucent, kSplashLayer, dot_->Handle().id, 0);
  for (const weather::Splash& s : sim.Splashes()) {
    const F32 t = s.age / weather::kSplashLife;
    const F32 d = 0.05f + 0.3f * t;
    // A tangent basis on the hit surface, so the disc lies ON it.
    const Vec3 n = s.normal;
    const Vec3 a =
        std::abs(n.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 u = Normalize(Cross(a, n));
    const Vec3 v = Cross(n, u);
    const Vec3 at = s.position + n * 0.02f;
    Mat4 world = Mat4::Identity();
    world.At(0, 0) = u.x * d;
    world.At(1, 0) = u.y * d;
    world.At(2, 0) = u.z * d;
    world.At(0, 1) = v.x * d;
    world.At(1, 1) = v.y * d;
    world.At(2, 1) = v.z * d;
    world.At(0, 2) = n.x;
    world.At(1, 2) = n.y;
    world.At(2, 2) = n.z;
    world.At(0, 3) = at.x;
    world.At(1, 3) = at.y;
    world.At(2, 3) = at.z;
    out.push_back(Renderable{
        .world = world,
        .size = Vec2{1.0f, 1.0f},
        .uv = Rect{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f},
        .color = Vec4{0.72f, 0.82f, 1.0f, 0.55f * (1.0f - t)},
        .texture = dot_->Handle(),
        .sort_key = key});
  }
}

}  // namespace lantern::view
