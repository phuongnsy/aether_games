// The deterministic core: a pure function of (state, LatchedInput, dt).
// No wall clock, no unseeded RNG (games/CLAUDE.md "Determinism & replay").
// A GameApi arrives with the first screen that needs one — not front-loaded.
#pragma once

#include <random>
#include <span>
#include <variant>
#include <vector>

#include "aether/core/types.hpp"
#include "tideworn/features/school/components.hpp"
#include "tideworn/features/voyage/components.hpp"
#include "tideworn/features/voyage/events.hpp"
#include "tideworn/runtime/snapshot.hpp"

namespace tideworn::runtime {

// Inputs latched per fixed step (the input-latch rule) — the step is a pure
// function of (world, THIS, dt), which is also what a replay records. The
// diver is the swim camera's position entering the sim as INPUT: fish flee
// it, and a replayed dive parts the same school the live one did.
struct LatchedInput {
  bool diver_present = false;
  aether::Vec2 diver_xz{0.0f, 0.0f};
};

// A storm strike, decided IN the fixed step from the world RNG so a replayed
// voyage flashes at the same second. `azimuth` is where the bolt stands on
// the horizon (radians on xz); view turns it into light, later thunder.
struct LightningStruck {
  aether::F32 azimuth = 0.0f;
};

using GameEvent = std::variant<voyage::BandChanged, LightningStruck>;

class GameWorld {
 public:
  explicit GameWorld(aether::U32 seed = 7) : rng_(seed) { SpawnSchools(seed); }

  void Step(const LatchedInput& input, aether::F32 dt);

  [[nodiscard]] ViewSnapshot Snapshot() const;
  // Frame-buffered events: view drains them after the frame's fixed steps.
  [[nodiscard]] std::span<const GameEvent> Events() const { return events_; }
  void ClearEvents() { events_.clear(); }

  [[nodiscard]] const voyage::VoyageState& Voyage() const { return voyage_; }

 private:
  void SpawnSchools(aether::U32 seed);

  voyage::VoyageState voyage_;
  std::vector<school::SchoolState> schools_;
  std::vector<GameEvent> events_;
  std::vector<voyage::BandChanged> voyage_events_;  // per-step scratch
  std::mt19937 rng_;  // THE world RNG; every future roll draws from it
  aether::Vec2 boat_pos_{0.0f, 0.0f};
  aether::F32 boat_heading_ = 0.0f;
  // Next lightning strike on the voyage clock; < 0 = none scheduled. Owned
  // here (not in the voyage feature) because it draws from the world RNG.
  aether::F32 next_strike_s_ = -1.0f;
};

}  // namespace tideworn::runtime
