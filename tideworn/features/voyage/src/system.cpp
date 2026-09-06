#include "tideworn/features/voyage/system.hpp"

#include <algorithm>
#include <cmath>

#include "tideworn/content/content.hpp"
#include "tideworn/features/voyage/tuning.hpp"

namespace tideworn::voyage {

using namespace aether;

F32 WindAt(F32 clock_s) {
  const auto& keys = content::kWindSchedule;
  const F32 period = keys.back().at_s;
  F32 t = std::fmod(clock_s, period);
  if (t < 0.0f) {
    t += period;
  }
  for (Usize i = 1; i < keys.size(); ++i) {
    if (t <= keys[i].at_s) {
      const F32 span = keys[i].at_s - keys[i - 1].at_s;
      const F32 u = span > 0.0f ? (t - keys[i - 1].at_s) / span : 0.0f;
      return keys[i - 1].wind_mps +
             (keys[i].wind_mps - keys[i - 1].wind_mps) * u;
    }
  }
  return keys.back().wind_mps;
}

Band BandFor(F32 wind_speed) {
  if (wind_speed >= kStormWind) {
    return Band::kStorm;
  }
  if (wind_speed >= kGaleWind) {
    return Band::kGale;
  }
  if (wind_speed >= kFreshWind) {
    return Band::kFresh;
  }
  return Band::kCalm;
}

void Step(VoyageState& state, F32 dt, std::vector<BandChanged>& out) {
  state.clock_s += std::max(dt, 0.0f);
  state.wind_speed = WindAt(state.clock_s);
  state.time_of_day = std::fmod(state.clock_s / content::kDayLengthS, 1.0f);
  const Band band = BandFor(state.wind_speed);
  if (band != state.band) {
    out.push_back(BandChanged{.from = state.band, .to = band});
    state.band = band;
  }
}

}  // namespace tideworn::voyage
