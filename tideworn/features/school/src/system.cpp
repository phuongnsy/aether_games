#include "tideworn/features/school/system.hpp"

#include <cmath>
#include <random>

#include "tideworn/content/content.hpp"
#include "tideworn/features/school/tuning.hpp"

namespace tideworn::school {

using namespace aether;

namespace {

[[nodiscard]] Vec3 ClampSpeed(Vec3 v, F32 max_speed) {
  const F32 len = Length(v);
  return len > max_speed ? v * (max_speed / len) : v;
}

// Radial escape from every close threat, accumulated into `accel`; true if
// any threat is inside the flee radius (which doubles the speed ceiling).
[[nodiscard]] bool Flee(const Env& env, Vec3 pos, Vec3& accel) {
  bool fleeing = false;
  for (U32 t = 0; t < env.threat_count; ++t) {
    const Vec2 away{pos.x - env.threats_xz[t].x, pos.z - env.threats_xz[t].y};
    const F32 threat_d = Length(away);
    if (threat_d >= kFleeRadius || threat_d <= 1e-3f) {
      continue;
    }
    fleeing = true;
    const F32 urgency = 1.0f - threat_d / kFleeRadius;
    accel.x += away.x / threat_d * kFlee * urgency;
    accel.z += away.y / threat_d * kFlee * urgency;
    accel.y -= 2.0f * urgency;  // fish dive from surface threats
  }
  return fleeing;
}

}  // namespace

void Spawn(SchoolState& state, U8 species, Vec2 anchor, U32 seed) {
  const content::Species& kind = content::kSpecies[species];
  state.species = species;
  state.clock_s = 0.0f;
  state.fish.clear();
  state.fish.reserve(kind.count);
  std::mt19937 rng(seed);
  std::uniform_real_distribution<F32> radial(0.0f, kind.home_radius * 0.6f);
  std::uniform_real_distribution<F32> angle(0.0f, 6.2831853f);
  std::uniform_real_distribution<F32> depth(kind.depth_min, kind.depth_max);
  for (U32 i = 0; i < kind.count; ++i) {
    const F32 a = angle(rng);
    const F32 r = radial(rng);
    const F32 heading = angle(rng);
    state.fish.push_back(
        Fish{.pos = Vec3{anchor.x + std::cos(a) * r, -depth(rng),
                         anchor.y + std::sin(a) * r},
             .vel = Vec3{std::cos(heading), 0.0f, std::sin(heading)} *
                    (kind.cruise_mps * 0.5f)});
  }
}

void Step(SchoolState& state, const Env& env, F32 dt) {
  if (state.fish.empty() || dt <= 0.0f) {
    return;
  }
  const content::Species& kind = content::kSpecies[state.species];
  state.clock_s += dt;

  Vec3 centroid{};
  Vec3 mean_vel{};
  for (const Fish& f : state.fish) {
    centroid = centroid + f.pos;
    mean_vel = mean_vel + f.vel;
  }
  const F32 inv = 1.0f / static_cast<F32>(state.fish.size());
  centroid = centroid * inv;
  mean_vel = mean_vel * inv;

  const F32 sep_radius = kSeparationRadius * kind.scale + 0.15f;
  const F32 sep_sq = sep_radius * sep_radius;
  const F32 band_mid = -0.5f * (kind.depth_min + kind.depth_max);
  const F32 band_half = 0.5f * (kind.depth_max - kind.depth_min);

  for (Usize i = 0; i < state.fish.size(); ++i) {
    Fish& f = state.fish[i];
    Vec3 accel{};

    // Separation: pairwise inside the small radius. O(N^2) on <=18 fish.
    for (Usize j = 0; j < state.fish.size(); ++j) {
      if (j == i) {
        continue;
      }
      const Vec3 away = f.pos - state.fish[j].pos;
      const F32 d2 = Dot(away, away);
      if (d2 < sep_sq && d2 > 1e-6f) {
        accel = accel + away * (kSeparation / (d2 / sep_sq) * inv);
      }
    }
    accel = accel + (centroid - f.pos) * kCohesion;
    accel = accel + (mean_vel - f.vel) * kAlignment;

    // Depth band: quadratic-onset spring — free inside, firm past the edge.
    const F32 off_band = (f.pos.y - band_mid) / band_half;
    if (std::abs(off_band) > 0.6f) {
      accel.y -= (off_band - std::copysign(0.6f, off_band)) * band_half *
                 kDepthSpring / 0.4f;
    }

    // Home: pull toward the anchor once outside the radius.
    const Vec2 to_home{env.anchor_xz.x - f.pos.x, env.anchor_xz.y - f.pos.z};
    const F32 home_d = Length(to_home);
    if (home_d > kind.home_radius) {
      const F32 pull = (home_d - kind.home_radius) * kHomeSpring;
      accel.x += to_home.x / home_d * pull;
      accel.z += to_home.y / home_d * pull;
    }

    // Wander: per-fish incommensurate sinusoids — deterministic, no RNG.
    const F32 phase = static_cast<F32>(i) * 2.399f;  // golden-angle spread
    accel.x += kWander * std::sin(state.clock_s * 0.7f + phase);
    accel.z += kWander * std::cos(state.clock_s * 0.53f + phase * 1.7f);

    const bool fleeing = Flee(env, f.pos, accel);

    f.vel = f.vel + (accel - f.vel * kDrag) * dt;
    // The speed ceiling RISES with distance from home — the anchor is a boat
    // under way (1.2 m/s), and a cruise slower than the boat would otherwise
    // strand every school astern within a minute, permanently out of sight.
    F32 ceiling = kind.cruise_mps * (fleeing ? 2.0f : 1.0f);
    if (home_d > kind.home_radius) {
      ceiling =
          std::max(ceiling, std::min(kCatchUpMax, (home_d - kind.home_radius) *
                                                      kCatchUpPerMetre));
    }
    f.vel = ClampSpeed(f.vel, ceiling);
    f.pos = f.pos + f.vel * dt;
    // The surface is a hard lid: a fish never breaches into the spray layer.
    f.pos.y = std::min(f.pos.y, -0.4f);
  }
}

}  // namespace tideworn::school
