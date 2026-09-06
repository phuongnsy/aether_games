#include "hf/features/production/system.hpp"

#include <span>

#include "hf/runtime/chain.hpp"
#include "hf/runtime/tick.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::production {
namespace {

using runtime::Building;
using runtime::Tick;

[[nodiscard]] Tick DurationOf(content::RecipeId recipe) {
  return runtime::TicksFromSeconds(content::RecipeById(recipe).seconds);
}

}  // namespace

bool ProductionSystem::TryQueue(runtime::StepContext& ctx, aether::U32 building,
                                content::RecipeId recipe,
                                runtime::EventList& out) {
  const std::span<Building> buildings = ctx.world.Buildings();
  if (building >= buildings.size() || !content::RecipeExists(recipe)) {
    return false;
  }
  Building& mill = buildings[building];
  if (mill.Full()) {
    out.emplace_back(
        runtime::Refused{.why = runtime::Refused::Why::kNoSpaceInQueue});
    return false;
  }
  const content::Recipe& spec = content::RecipeById(recipe);
  // All-or-nothing, and BEFORE anything else changes: a recipe half-paid-for is
  // not a state the deadline loop below could make sense of.
  if (!ctx.world.TheBarn().Take(spec.input, spec.input_count)) {
    out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoItems});
    return false;
  }
  const bool was_idle = !mill.Busy();
  mill.queue[mill.queued] = recipe;
  ++mill.queued;
  if (was_idle) {
    mill.done_tick = ctx.world.Now() + DurationOf(recipe);
  }
  out.emplace_back(runtime::Queued{.building = building, .recipe = recipe});
  return true;
}

void ProductionSystem::Step(runtime::StepContext& ctx,
                            const runtime::EventList& /*in*/,
                            runtime::EventList& out) {
  if (ctx.input.queue_recipe != runtime::LatchedInput::kNoRecipe) {
    (void)TryQueue(ctx, ctx.input.queue_building, ctx.input.queue_recipe, out);
  }

  const Tick now = ctx.world.Now();
  const std::span<Building> buildings = ctx.world.Buildings();
  for (aether::Usize i = 0; i < buildings.size(); ++i) {
    Building& mill = buildings[i];

    // GEA §16.8.9.1's dispatch loop, and it is the whole offline model in four
    // lines: "an event is only handled when the current game clock matches or
    // exceeds its delivery time". A loop over absolute deadlines does not care
    // whether the clock advanced one tick or a million, which is why a night
    // away costs one pass rather than 1.7 million steps.
    while (mill.Busy() && now >= mill.done_tick) {
      const content::RecipeId recipe = mill.queue[0];
      const content::Recipe& spec = content::RecipeById(recipe);
      const aether::U32 stored =
          ctx.world.TheBarn().Add(spec.output, spec.output_count);
      out.emplace_back(runtime::Produced{
          .building = static_cast<aether::U32>(i), .recipe = recipe});
      out.emplace_back(runtime::Deposited{.item = spec.output,
                                          .stored = stored,
                                          .lost = spec.output_count - stored});

      for (aether::U8 k = 1; k < mill.queued; ++k) {
        mill.queue[k - 1] = mill.queue[k];
      }
      --mill.queued;
      // The next item starts WHEN THE LAST ONE FINISHED, not now. Chaining from
      // `now` would give a jumped step a different schedule from the same run
      // stepped one tick at a time, and the equivalence test would say so.
      if (mill.Busy()) {
        mill.done_tick += DurationOf(mill.queue[0]);
      }
    }
  }
}

}  // namespace hearthfield::production
