// A gameplay system: one pure step over shared world state, reading input and
// the events so far, appending its own. No side effects — those are view's.
#pragma once

#include "aether/core/types.hpp"
#include "hf/runtime/events.hpp"
#include "hf/runtime/latched_input.hpp"

namespace hearthfield::runtime {

class WorldView;

// Everything a system needs for one fixed step. `dt` is the fixed step's own
// seconds and exists for continuous motion; anything that must survive the
// process reads world.Now() instead, because a float accumulated per step
// cannot absorb an eight-hour absence.
struct StepContext {
  WorldView& world;
  const LatchedInput& input;
  aether::F32 dt;
};

class SimSystem {
 public:
  virtual ~SimSystem() = default;
  // `in` = events emitted so far this step by earlier systems; append to `out`.
  virtual void Step(StepContext& ctx, const EventList& in, EventList& out) = 0;
};

}  // namespace hearthfield::runtime
