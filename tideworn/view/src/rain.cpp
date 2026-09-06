#include "tideworn/view/rain.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "aether/rhi/device.hpp"

namespace tideworn::view {

using namespace aether;

namespace {

// Drops per second by band. Storm rain is a wall, gale rain a nuisance; both
// under the field's capacity so the ring never rotates drops out mid-fall.
constexpr F32 kGaleRate = 170.0f;
constexpr F32 kStormRate = 480.0f;

// A soft vertical streak (lantern's weather texture recipe — a game owns its
// assets, so it carries its own copy).
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

}  // namespace

Result<void> Rain::Load(rhi::Device& device) {
  streak_ = MakeStreak(device);
  if (!streak_) {
    return Fail(Errc::kInitFailed, "rain streak texture create failed");
  }
  weather::RainConfig3 config;
  config.capacity = 960;
  config.spawn_rate = 0.0f;  // the band drives it per Step
  config.spawn_y = 20.0f;
  config.spawn_height = 6.0f;  // a band, not a sheet — depth from frame one
  // TIGHT around the viewer: rain only reads within a few tens of metres, so
  // a wide box spends the whole budget where no streak subtends a pixel.
  config.spawn_min = Vec2{-15.0f, -15.0f};
  config.spawn_max = Vec2{15.0f, 15.0f};
  config.width = 0.025f;
  config.stretch = 0.035f;
  // Die AT the mean sea surface. In a trough that is ~2 m early — at 13 m/s
  // of fall that is 0.15 s, unreadable — while killing below the troughs
  // would let submerged drops draw OVER the waves (rain renders post-water).
  config.kill_below_y = 0.05f;
  config.texture = streak_->Handle();
  config.fade_begin = 24.0f;
  config.fade_end = 42.0f;
  config.alpha_jitter = 0.35f;
  field_.emplace(config);
  field_->SetEmitting(false);
  return {};
}

void Rain::Unload(rhi::Device& /*device*/) {
  field_.reset();
  streak_.reset();
}

void Rain::Step(const runtime::ViewSnapshot& snapshot, Vec2 wind_dir,
                Vec2 center) {
  if (!field_) {
    return;
  }
  const F32 clock = snapshot.clock_s;
  const F32 dt =
      last_clock_ < 0.0f ? 0.0f : std::clamp(clock - last_clock_, 0.0f, 0.25f);
  last_clock_ = clock;
  if (dt <= 0.0f) {
    return;
  }
  const bool storm = snapshot.band == voyage::Band::kStorm;
  const bool gale = snapshot.band == voyage::Band::kGale;
  field_->SetEmitting(storm || gale);
  F32 rate = 0.0f;
  if (storm) {
    rate = kStormRate;
  } else if (gale) {
    rate = kGaleRate;
  }
  field_->SetSpawnRate(rate);
  field_->SetSpawnCenter(center);
  // Wind shears the fall downwind — an acceleration, so long drops curve.
  const Vec2 w = Normalize(wind_dir);
  field_->SetWind(Vec3{w.x, 0.0f, w.y} * (0.45f * snapshot.wind_speed));
  impacts_.clear();
  field_->Step(dt, open_sea_, open_sky_, impacts_);
}

void Rain::Emit(std::vector<Renderable>& out, const Camera& camera) const {
  if (field_) {
    field_->Emit(out, camera);
  }
}

bool Rain::Raining() const { return field_ && field_->Emitting(); }

F32 Rain::Rate() const { return field_ ? field_->SpawnRate() : 0.0f; }

}  // namespace tideworn::view
