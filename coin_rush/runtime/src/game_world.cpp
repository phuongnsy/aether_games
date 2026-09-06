#include "cr/runtime/game_world.hpp"

namespace game {

const EventList& GameWorld::Step(const LatchedInput& in, aether::F32 dt) {
  events_.clear();
  StepContext ctx{.world = world_, .input = in, .dt = dt};
  // Systems run in registration order; each sees the events emitted so far this
  // step (by earlier systems) and appends its own.
  for (SimSystem* sys : systems_) {
    scratch_.clear();
    sys->Step(ctx, events_, scratch_);
    events_.insert(events_.end(), scratch_.begin(), scratch_.end());
  }
  return events_;
}

}  // namespace game
