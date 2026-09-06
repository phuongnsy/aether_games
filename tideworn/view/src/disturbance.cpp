#include "tideworn/view/disturbance.hpp"

#include <algorithm>
#include <cmath>

namespace tideworn::view {

using namespace aether;

namespace {
constexpr F32 kCell = Disturbance::kExtent / Disturbance::kResolution;
// Ripple propagation speed. Short splash rings travel ~1.5-2.5 m/s (the same
// deep-water speeds the analytic pool used); the CFL bound c*dt/dx < 0.7
// holds comfortably at 60 Hz steps (2.2 * 0.017 / 0.28 = 0.13).
constexpr F32 kWaveSpeed = 2.2f;
// Energy decay per second; a ring should die in a couple of seconds, like the
// pool's kRippleDecay, or the window fills with standing chop.
constexpr F32 kDamping = 1.6f;
}  // namespace

void Disturbance::Splash(Vec2 world_xz, F32 amount) {
  const F32 gx = (world_xz.x - origin_.x) / kCell;
  const F32 gy = (world_xz.y - origin_.y) / kCell;
  const int cx = static_cast<int>(std::round(gx));
  const int cy = static_cast<int>(std::round(gy));
  // A 2-cell gaussian stamp: a point impulse on a grid rings at the Nyquist
  // checkerboard; a small blob launches a clean circular wave.
  for (int y = cy - 2; y <= cy + 2; ++y) {
    for (int x = cx - 2; x <= cx + 2; ++x) {
      if (x < 1 || y < 1 || x >= static_cast<int>(kResolution) - 1 ||
          y >= static_cast<int>(kResolution) - 1) {
        continue;
      }
      const F32 dx = static_cast<F32>(x) - gx;
      const F32 dy = static_cast<F32>(y) - gy;
      const F32 g = std::exp(-(dx * dx + dy * dy) * 0.7f);
      height_[Index(static_cast<U32>(x), static_cast<U32>(y))] -= amount * g;
    }
  }
}

void Disturbance::ShiftCells(int dx, int dy) {
  auto shift = [&](std::vector<F32>& field) {
    std::ranges::fill(scratch_, 0.0f);
    for (U32 y = 0; y < kResolution; ++y) {
      const int sy = static_cast<int>(y) + dy;
      if (sy < 0 || sy >= static_cast<int>(kResolution)) {
        continue;
      }
      for (U32 x = 0; x < kResolution; ++x) {
        const int sx = static_cast<int>(x) + dx;
        if (sx < 0 || sx >= static_cast<int>(kResolution)) {
          continue;
        }
        scratch_[Index(x, y)] =
            field[Index(static_cast<U32>(sx), static_cast<U32>(sy))];
      }
    }
    field.swap(scratch_);
  };
  shift(height_);
  shift(prev_);
  origin_.x += static_cast<F32>(dx) * kCell;
  origin_.y += static_cast<F32>(dy) * kCell;
}

void Disturbance::Step(F32 dt, Vec2 center) {
  dt = std::clamp(dt, 0.0f, 0.25f);
  if (dt <= 0.0f) {
    return;
  }
  // Recentre in WHOLE cells once the window drifts an eighth off — the field
  // stays world-anchored, only the window moves.
  const Vec2 want{center.x - kExtent * 0.5f, center.y - kExtent * 0.5f};
  const int dx = static_cast<int>(std::round((want.x - origin_.x) / kCell));
  const int dy = static_cast<int>(std::round((want.y - origin_.y) / kCell));
  if (std::abs(dx) > static_cast<int>(kResolution) / 8 ||
      std::abs(dy) > static_cast<int>(kResolution) / 8) {
    ShiftCells(dx, dy);
  }

  // SUBSTEPPED to the CFL bound (c*h/dx < ~0.7): a compressed voyage clock
  // (--time-scale) hands this 0.25 s frames, and one verlet step that size is
  // UNSTABLE — the rings dissolved into over-damped mush before anything
  // could be seen, which read as "the grid does nothing".
  constexpr F32 kMaxSubstep = 0.05f;
  const int substeps =
      std::max(1, static_cast<int>(std::ceil(dt / kMaxSubstep)));
  const F32 h = dt / static_cast<F32>(substeps);
  const F32 c2 = (kWaveSpeed * h / kCell) * (kWaveSpeed * h / kCell);
  const F32 damp = std::exp(-kDamping * h);
  for (int s = 0; s < substeps; ++s) {
    for (U32 y = 1; y < kResolution - 1; ++y) {
      for (U32 x = 1; x < kResolution - 1; ++x) {
        const Usize i = Index(x, y);
        const F32 lap = height_[i - 1] + height_[i + 1] +
                        height_[i - kResolution] + height_[i + kResolution] -
                        4.0f * height_[i];
        // Velocity-damped verlet: the ring loses energy, not shape.
        scratch_[i] = height_[i] + (height_[i] - prev_[i]) * damp + c2 * lap;
      }
    }
    prev_.swap(height_);
    height_.swap(scratch_);
  }
}

void Disturbance::EncodeGradient(std::vector<U8>& out) const {
  out.resize(static_cast<Usize>(kResolution) * kResolution * 4);
  for (U32 y = 0; y < kResolution; ++y) {
    for (U32 x = 0; x < kResolution; ++x) {
      const U32 x0 = x > 0 ? x - 1 : x;
      const U32 x1 = x < kResolution - 1 ? x + 1 : x;
      const U32 y0 = y > 0 ? y - 1 : y;
      const U32 y1 = y < kResolution - 1 ? y + 1 : y;
      const F32 gx =
          (height_[Index(x1, y)] - height_[Index(x0, y)]) / (kCell * 2.0f);
      const F32 gy =
          (height_[Index(x, y1)] - height_[Index(x, y0)]) / (kCell * 2.0f);
      const Usize i = (static_cast<Usize>(y) * kResolution + x) * 4;
      out[i] =
          static_cast<U8>(std::clamp(gx / kDecode + 0.5f, 0.0f, 1.0f) * 255.0f);
      out[i + 1] =
          static_cast<U8>(std::clamp(gy / kDecode + 0.5f, 0.0f, 1.0f) * 255.0f);
      out[i + 2] = 0;
      out[i + 3] = 255;
    }
  }
}

Vec4 Disturbance::Region() const {
  return Vec4{origin_.x, origin_.y, 1.0f / kExtent, kDecode};
}

F32 Disturbance::HeightAt(Vec2 world_xz) const {
  const F32 gx = (world_xz.x - origin_.x) / kCell;
  const F32 gy = (world_xz.y - origin_.y) / kCell;
  if (gx < 0.0f || gy < 0.0f || gx > kResolution - 2 || gy > kResolution - 2) {
    return 0.0f;
  }
  const U32 x = static_cast<U32>(gx);
  const U32 y = static_cast<U32>(gy);
  const F32 fx = gx - static_cast<F32>(x);
  const F32 fy = gy - static_cast<F32>(y);
  const F32 a =
      height_[Index(x, y)] * (1.0f - fx) + height_[Index(x + 1, y)] * fx;
  const F32 b = height_[Index(x, y + 1)] * (1.0f - fx) +
                height_[Index(x + 1, y + 1)] * fx;
  return a * (1.0f - fy) + b * fy;
}

}  // namespace tideworn::view
