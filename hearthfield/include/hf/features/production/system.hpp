// Production: buildings that turn one item into another on a timer.
//
// THE INPUTS ARE COMMITTED AT QUEUE TIME. Queuing debits the barn there and
// then, so a building's future depends on nothing but its own queue and the
// clock. A mill that instead reached into the barn when it got hungry would be
// exactly the "building pulling from a shared pool" the Hearthfield spec's risk
// 5 names as what turns an absence into a real simulation — and the offline
// equivalence test is what would tell us, loudly.
#pragma once

#include "hf/content/recipes.hpp"
#include "hf/runtime/events.hpp"
#include "hf/runtime/sim_system.hpp"

namespace hearthfield::production {

class ProductionSystem final : public runtime::SimSystem {
 public:
  void Step(runtime::StepContext& ctx, const runtime::EventList& in,
            runtime::EventList& out) override;

 private:
  // Debit the barn and append. Returns false (and says why) if the queue is
  // full or the ingredients are not there.
  static bool TryQueue(runtime::StepContext& ctx, aether::U32 building,
                       content::RecipeId recipe, runtime::EventList& out);
};

}  // namespace hearthfield::production
