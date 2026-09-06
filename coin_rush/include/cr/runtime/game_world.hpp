// The game's deterministic fixed-step core. Step() runs the app-ordered systems
// over (world, input, dt) with a seeded RNG, no wall clock; replay works.
#pragma once

#include <vector>

#include "aether/core/types.hpp"
#include "cr/runtime/events.hpp"
#include "cr/runtime/latched_input.hpp"
#include "cr/runtime/rng.hpp"
#include "cr/runtime/sim_system.hpp"
#include "cr/runtime/view_snapshot.hpp"
#include "cr/runtime/world_view.hpp"

namespace game {

class GameWorld {
 public:
  explicit GameWorld(aether::U64 seed = 1) : rng_(seed) {}

  // App registers systems in the order they should run each step.
  void AddSystem(SimSystem& sys) { systems_.push_back(&sys); }

  // Advance one fixed step; returns the events emitted this step.
  const EventList& Step(const LatchedInput& in, aether::F32 dt);

  [[nodiscard]] WorldView& World() { return world_; }
  [[nodiscard]] const WorldView& World() const { return world_; }
  [[nodiscard]] Rng& Random() { return rng_; }
  [[nodiscard]] const ViewSnapshot& Snapshot() const { return snapshot_; }
  [[nodiscard]] ViewSnapshot& Snapshot() { return snapshot_; }

 private:
  WorldView world_;
  Rng rng_;
  std::vector<SimSystem*> systems_;
  EventList events_;   // accumulated this step
  EventList scratch_;  // one system's output, merged into events_
  ViewSnapshot snapshot_;
};

}  // namespace game
