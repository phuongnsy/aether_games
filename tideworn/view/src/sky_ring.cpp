#include "tideworn/view/sky_ring.hpp"

#include <algorithm>
#include <cmath>

namespace tideworn::view {

using namespace aether;

namespace {

// Converts a keyframe sun's measured irradiance luminance into a directional
// light intensity. Anchored so the original single-bake ocean (luminance
// 0.192 at 26° elevation) reproduces the intensity the scenes were lit with.
constexpr F32 kIntensityPerLuminance = 3.4f / 0.192f;

[[nodiscard]] F32 Luminance(Vec3 c) {
  return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

}  // namespace

SkyRing::State SkyRing::Evaluate(F32 time_of_day) const {
  State state;
  if (!Ready()) {
    return state;
  }
  F32 t = time_of_day - std::floor(time_of_day);
  // Find the segment [keys_[i], keys_[i+1]) containing t, wrapping the last
  // key forward by a whole day.
  Usize i = keys_.size() - 1;
  for (Usize k = 0; k + 1 < keys_.size(); ++k) {
    if (t >= keys_[k].at && t < keys_[k + 1].at) {
      i = k;
      break;
    }
  }
  const Key& a = keys_[i];
  const Key& b = keys_[(i + 1) % keys_.size()];
  const F32 span = (i + 1 < keys_.size() ? b.at : b.at + 1.0f) - a.at;
  const F32 local = t >= a.at ? t - a.at : t + 1.0f - a.at;
  const F32 w = span > 0.0f ? std::clamp(local / span, 0.0f, 1.0f) : 0.0f;
  if (!a.env || !b.env) {
    return state;
  }

  state.valid = true;
  state.cube_a = a.env->Specular();
  state.cube_b = b.env->Specular();
  state.blend = w;
  state.ladder_levels = a.env->LadderLevels();
  const auto sha = a.env->Irradiance();
  const auto shb = b.env->Irradiance();
  for (Usize c = 0; c < state.irradiance.size(); ++c) {
    state.irradiance[c] = Lerp(sha[c], shb[c], w);
  }

  // The blended sun. Direction lerp + renormalize is fine at these key
  // spacings (the neighbours are 45° apart at most, nowhere near opposed).
  const auto& sun_a = a.env->Sun();
  const auto& sun_b = b.env->Sun();
  if (sun_a && sun_b) {
    const Vec3 toward = Normalize(Lerp(sun_a->direction, sun_b->direction, w));
    state.sun_travel = toward * -1.0f;
    state.sun_color = Lerp(sun_a->color, sun_b->color, w);
    const F32 lum = Luminance(Lerp(sun_a->irradiance, sun_b->irradiance, w));
    state.sun_intensity = kIntensityPerLuminance * std::max(lum, 0.0f);
  }
  return state;
}

}  // namespace tideworn::view
