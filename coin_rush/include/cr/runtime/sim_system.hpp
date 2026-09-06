// A gameplay system: one pure step over shared world state, reading input + the
// events so far, appending its own. No side effects — those are view's.
#pragma once

#include "aether/core/types.hpp"
#include "cr/runtime/events.hpp"
#include "cr/runtime/latched_input.hpp"

namespace game {

class WorldView;  // the shared sim state (defined in world_view.hpp)

// Everything a system needs for one fixed step.
struct StepContext {
  WorldView& world;
  const LatchedInput& input;
  aether::F32 dt;
};

class SimSystem {
 public:
  virtual ~SimSystem() = default;
  // `in` = events emitted so far this step (by earlier systems); append to
  // `out`.
  virtual void Step(StepContext& ctx, const EventList& in, EventList& out) = 0;
};

}  // namespace game
