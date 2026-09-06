#include "tideworn/runtime/game_world.hpp"

#include <cmath>

#include "tideworn/content/content.hpp"
#include "tideworn/features/school/system.hpp"
#include "tideworn/features/voyage/system.hpp"

namespace tideworn::runtime {

using namespace aether;

void GameWorld::SpawnSchools(U32 seed) {
  schools_.resize(content::kSpecies.size());
  for (U8 s = 0; s < static_cast<U8>(schools_.size()); ++s) {
    // Distinct per-school seeds derived from the world seed: deterministic,
    // and independent of the world RNG's draw order.
    school::Spawn(schools_[s], s, boat_pos_, seed * 31u + s);
  }
}

void GameWorld::Step(const LatchedInput& input, F32 dt) {
  voyage_events_.clear();
  voyage::Step(voyage_, dt, voyage_events_);
  for (const voyage::BandChanged& e : voyage_events_) {
    events_.emplace_back(e);
  }
  // The boat holds a slow course; sailing input arrives with its own feature
  // slice. Constant speed on the fixed step keeps replays exact.
  constexpr F32 kKnots = 1.2f;
  boat_pos_.x += std::cos(boat_heading_) * kKnots * dt;
  boat_pos_.y += std::sin(boat_heading_) * kKnots * dt;

  // LIGHTNING: storm-only, scheduled from the world RNG on the voyage clock —
  // a replay meets the same bolt at the same second. Draw order is part of
  // determinism: consume the RNG only when a decision is actually due.
  if (voyage_.band == voyage::Band::kStorm) {
    if (next_strike_s_ < 0.0f) {
      std::uniform_real_distribution<F32> wait(2.5f, 9.0f);
      next_strike_s_ = voyage_.clock_s + wait(rng_);
    } else if (voyage_.clock_s >= next_strike_s_) {
      std::uniform_real_distribution<F32> angle(0.0f, 6.2831853f);
      events_.emplace_back(LightningStruck{.azimuth = angle(rng_)});
      std::uniform_real_distribution<F32> wait(2.5f, 9.0f);
      next_strike_s_ = voyage_.clock_s + wait(rng_);
    }
  } else {
    next_strike_s_ = -1.0f;
  }

  // Marine life follows the boat (the diving loop happens where the player
  // is); the hull is also the thing shallow fish flee. The anchor LEADS the
  // boat by the school's own radius: homing equilibrium sits home_radius+2.4m
  // BEHIND the anchor for a boat under way, so an anchor at the boat parks
  // every fish astern at the edge of the fog — never around the player.
  const Vec2 ahead{std::cos(boat_heading_), std::sin(boat_heading_)};
  for (school::SchoolState& s : schools_) {
    const F32 lead = content::kSpecies[s.species].home_radius;
    school::Env env{.anchor_xz = boat_pos_ + ahead * lead,
                    .threats_xz = {boat_pos_},
                    .threat_count = 1};
    if (input.diver_present) {
      env.threats_xz[env.threat_count++] = input.diver_xz;
    }
    school::Step(s, env, dt);
  }
}

ViewSnapshot GameWorld::Snapshot() const {
  std::vector<FishInstance> fish;
  Usize total = 0;
  for (const school::SchoolState& s : schools_) {
    total += s.fish.size();
  }
  fish.reserve(total);
  U32 index = 0;
  for (const school::SchoolState& s : schools_) {
    for (const school::Fish& f : s.fish) {
      // Golden-angle phase spread: unique per fish, stable across frames.
      fish.push_back(FishInstance{.pos = f.pos,
                                  .vel = f.vel,
                                  .species = s.species,
                                  .phase = static_cast<F32>(index) * 0.382f});
      ++index;
    }
  }
  return ViewSnapshot{.clock_s = voyage_.clock_s,
                      .wind_speed = voyage_.wind_speed,
                      .time_of_day = voyage_.time_of_day,
                      .band = voyage_.band,
                      .boat_pos = boat_pos_,
                      .boat_heading = boat_heading_,
                      .fish = std::move(fish),
                      .hud = HudData{.wind_speed = voyage_.wind_speed,
                                     .time_of_day = voyage_.time_of_day,
                                     .band = voyage_.band}};
}

}  // namespace tideworn::runtime
