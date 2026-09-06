// The deterministic core: Step() is a pure function of (world, LatchedInput,
// dt). No wall clock, no unseeded RNG (games/CLAUDE.md "Determinism & replay").
#pragma once

#include <vector>

#include "aether/core/types.hpp"
#include "hf/runtime/events.hpp"
#include "hf/runtime/latched_input.hpp"
#include "hf/runtime/sim_system.hpp"
#include "hf/runtime/snapshot.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::runtime {

class GameWorld {
 public:
  explicit GameWorld(aether::U64 seed = 1, aether::Usize plot_count = 1)
      : world_(seed, plot_count) {}

  // App registers systems in the order they run each step — plots, production,
  // livestock, orders, economy (spec §5.1). Explicit, so a crop harvested this
  // step can feed a building this step.
  void AddSystem(SimSystem& system) { systems_.push_back(&system); }

  // Advance one fixed step; returns the events emitted during it.
  const EventList& Step(const LatchedInput& input, aether::F32 dt);

  [[nodiscard]] WorldView& World() { return world_; }
  [[nodiscard]] const WorldView& World() const { return world_; }
  [[nodiscard]] const ViewSnapshot& Snapshot() const { return snapshot_; }

 private:
  void BuildSnapshot();

  WorldView world_;
  std::vector<SimSystem*> systems_;
  EventList events_;   // accumulated this step
  EventList scratch_;  // one system's output, merged into events_
  ViewSnapshot snapshot_;
};

}  // namespace hearthfield::runtime
