#include "hf/runtime/game_world.hpp"

#include "hf/content/animals.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/farm.hpp"
#include "hf/runtime/weather.hpp"

namespace hearthfield::runtime {

const EventList& GameWorld::Step(const LatchedInput& input, aether::F32 dt) {
  events_.clear();

  // The ONLY place the farm clock moves, and the whole offline model in one
  // line. `offline_ticks` is elapsed real time that entered as input (spec
  // §4.2), so a step that absorbs an absence is indistinguishable to every
  // system from a run of ordinary steps — which is why catch-up needs no
  // special code path and why the equivalence is testable (h0f).
  world_.AdvanceBy(Tick{1} + input.offline_ticks);

  for (SimSystem* system : systems_) {
    StepContext ctx{.world = world_, .input = input, .dt = dt};
    scratch_.clear();
    system->Step(ctx, events_, scratch_);
    events_.insert(events_.end(), scratch_.begin(), scratch_.end());
  }

  BuildSnapshot();
  return events_;
}

void GameWorld::BuildSnapshot() {
  const std::span<const Plot> plots = world_.Plots();
  snapshot_.tick = world_.Now();
  snapshot_.ready_count = 0;
  // Resized, never reassigned: the vector's storage survives every step after
  // the first, so the snapshot costs no allocation in the steady state.
  snapshot_.plots.resize(plots.size());

  for (aether::Usize i = 0; i < plots.size(); ++i) {
    const Plot& plot = plots[i];
    snapshot_.plots[i] = PlotView{
        .state = plot.state,
        .crop = plot.crop,
        .growth = GrowthFraction(plot, snapshot_.tick),
    };
    if (plot.state == PlotState::kReady) {
      ++snapshot_.ready_count;
    }
  }

  snapshot_.unlocked = world_.TheLand().owned;
  snapshot_.coin = world_.ThePurse().coin;
  snapshot_.barn_total = world_.TheBarn().Total();
  snapshot_.barn_capacity = world_.TheBarn().capacity;
  snapshot_.islands_unlocked = world_.Isles().unlocked;
  snapshot_.island = world_.Isles().current;
  snapshot_.milling = 0;
  // Cleared and refilled rather than resized in place: the table only grows
  // (a farm gains buildings and never loses one), so this is a copy of at most
  // 64 small records and never an allocation after the first few.
  snapshot_.buildings.clear();
  for (const Building& building : world_.Buildings()) {
    snapshot_.milling += building.queued;
    snapshot_.buildings.push_back(BuildingView{
        .kind = building.kind, .cell = building.cell, .busy = building.Busy()});
  }
  snapshot_.items = world_.TheBarn().counts;
  snapshot_.orders = world_.Board().slots;

  const Coop& coop = world_.TheCoop();
  snapshot_.animals = coop.animals;
  snapshot_.coop_fed = coop.Fed(snapshot_.tick);
  snapshot_.fed_ticks =
      snapshot_.coop_fed ? coop.fed_until - snapshot_.tick : Tick{0};
  // Weather reaches view THROUGH the snapshot like everything else, even
  // though it is a pure function view could call itself. One channel (law 2),
  // and it costs a bool.
  snapshot_.raining = RainingAt(snapshot_.tick);
}

}  // namespace hearthfield::runtime
