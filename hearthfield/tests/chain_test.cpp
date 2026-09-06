// The chain: refine, ship, expand — and the property all three must not break.
//
// The headline case is at the bottom. H0 made the spec's risk 5 mechanically
// testable and H3 is the milestone that meets it: a mill drawing from the barn
// IS the "building pulling from a shared pool" that would turn an absence into
// a real simulation. The equivalence test is what says whether the design
// avoided the coupling or merely avoided thinking about it.
#include <doctest/doctest.h>

#include <variant>
#include <vector>

#include "hf/content/items.hpp"
#include "hf/content/recipes.hpp"
#include "hf/features/economy/system.hpp"
#include "hf/features/orders/system.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/features/production/system.hpp"
#include "hf/runtime/game_world.hpp"
#include "hf/runtime/save.hpp"

using namespace aether;
using hearthfield::economy::EconomySystem;
using hearthfield::orders::OrdersSystem;
using hearthfield::plots::PlotsSystem;
using hearthfield::production::ProductionSystem;
using hearthfield::runtime::GameWorld;
using hearthfield::runtime::LatchedInput;
using hearthfield::runtime::PlotState;
namespace content = hearthfield::content;
namespace runtime = hearthfield::runtime;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;

// A farm with the whole chain registered, in the spec's §5.1 order.
struct Chain {
  GameWorld world;
  PlotsSystem plots;
  ProductionSystem production;
  OrdersSystem orders;
  EconomySystem economy;

  explicit Chain(Usize count = 9, U64 seed = 1) : world(seed, count) {
    world.World().SetBuildingCount(1);
    world.World().TheLand().owned = static_cast<U32>(count);
    world.AddSystem(plots);
    world.AddSystem(production);
    world.AddSystem(orders);
    world.AddSystem(economy);
  }

  [[nodiscard]] runtime::Barn& Barn() { return world.World().TheBarn(); }
  [[nodiscard]] runtime::Building& Mill() {
    return world.World().Buildings()[0];
  }
};

template <class E>
[[nodiscard]] bool Contains(const runtime::EventList& events) {
  for (const runtime::GameEvent& event : events) {
    if (std::holds_alternative<E>(event)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] runtime::Tick GrindTicks() {
  return runtime::TicksFromSeconds(
      content::RecipeById(content::kGrindWheat).seconds);
}

}  // namespace

TEST_CASE("a harvest reaches the barn through EVENTS, not a direct call") {
  // The dependency law working: plots knows nothing about a barn, it only says
  // what came off the ground, and economy — running last — reads that from the
  // same step's event list.
  Chain chain;
  const auto grow = static_cast<U32>(runtime::TicksFromSeconds(
      content::CropById(content::kWheat).grow_seconds));
  chain.world.Step(LatchedInput{.tap = true, .hovered = 0}, kDt);
  chain.world.Step(LatchedInput{.offline_ticks = grow}, kDt);
  REQUIRE(chain.world.World().Plots()[0].state == PlotState::kReady);

  const runtime::EventList& events =
      chain.world.Step(LatchedInput{.tap = true, .hovered = 0}, kDt);
  CHECK(Contains<runtime::Harvested>(events));
  CHECK(Contains<runtime::Deposited>(events));
  CHECK(chain.Barn().Of(content::kWheatItem) ==
        content::CropById(content::kWheat).yield);
}

TEST_CASE("THE BARN CAP REFUSES, AND SAYS SO RATHER THAN SWALLOWING") {
  Chain chain;
  chain.Barn().capacity = 3;
  CHECK(chain.Barn().Add(content::kWheatItem, 10) == 3);  // partial
  CHECK(chain.Barn().Total() == 3);
  CHECK(chain.Barn().Add(content::kWheatItem, 10) == 0);  // then nothing
  CHECK(chain.Barn().Space() == 0);

  SUBCASE("a harvest into a full barn emits Refused, not silence") {
    chain.world.World().TheLand().owned = 9;
    const auto grow = static_cast<U32>(runtime::TicksFromSeconds(
        content::CropById(content::kWheat).grow_seconds));
    chain.world.Step(LatchedInput{.tap = true, .hovered = 0}, kDt);
    chain.world.Step(LatchedInput{.offline_ticks = grow}, kDt);
    const runtime::EventList& events =
        chain.world.Step(LatchedInput{.tap = true, .hovered = 0}, kDt);
    CHECK(Contains<runtime::Refused>(events));
  }
}

TEST_CASE("QUEUING DEBITS THE BARN IMMEDIATELY — the inputs are committed") {
  // §3(a) of the plan, and the reason the whole milestone stays closed-form.
  Chain chain;
  chain.Barn().Add(content::kWheatItem, 4);
  const content::Recipe& recipe = content::RecipeById(content::kGrindWheat);

  chain.world.Step(LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);
  CHECK(chain.Barn().Of(content::kWheatItem) == 4 - recipe.input_count);
  CHECK(chain.Mill().queued == 1);

  SUBCASE("and refuses when the ingredients are not there") {
    chain.Barn().counts[content::kWheatItem] = 0;
    const runtime::EventList& events = chain.world.Step(
        LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);
    CHECK(Contains<runtime::Refused>(events));
    CHECK(chain.Mill().queued == 1);  // unchanged
  }
}

TEST_CASE("A JUMPED STEP DRAINS A THREE-DEEP QUEUE, IN ORDER") {
  // GEA §16.8.9.1's dispatch loop: an absence is the same loop with a bigger
  // `now`. Each item starts when the PREVIOUS one finished, so three grinds
  // take three durations and not one.
  Chain chain;
  chain.Barn().Add(content::kWheatItem, 6);
  chain.world.Step(LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);
  chain.world.Step(LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);
  chain.world.Step(LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);
  REQUIRE(chain.Mill().queued == 3);

  SUBCASE("one duration finishes exactly one") {
    chain.world.Step(
        LatchedInput{.offline_ticks = static_cast<U32>(GrindTicks())}, kDt);
    CHECK(chain.Mill().queued == 2);
    CHECK(chain.Barn().Of(content::kFlour) == 1);
  }
  SUBCASE("three durations finish all three, in one step") {
    chain.world.Step(
        LatchedInput{.offline_ticks = static_cast<U32>(3 * GrindTicks())}, kDt);
    CHECK(chain.Mill().queued == 0);
    CHECK(chain.Barn().Of(content::kFlour) == 3);
  }
}

TEST_CASE("THE ORDER BOARD POSTS AT MOST THREE TIMES ACROSS A MONTH AWAY") {
  // The catch-up bound, and it is a GAME RULE rather than a clamp: the board
  // only posts into a free slot. A loop that advanced the deadline one interval
  // at a time would turn thirty days into 1440 turns.
  Chain chain;
  chain.world.Step(LatchedInput{}, kDt);  // seeds next_refresh
  REQUIRE(chain.world.World().Board().next_refresh > 0);

  chain.world.Step(
      LatchedInput{.offline_ticks = static_cast<U32>(
                       runtime::kMaxOfflineSeconds * runtime::kTicksPerSecond)},
      kDt);
  CHECK(chain.world.World().Board().Full());
  U32 active = 0;
  for (const runtime::Order& order : chain.world.World().Board().slots) {
    active += order.active ? 1 : 0;
    if (order.active) {
      CHECK(content::ItemExists(order.item));
      CHECK(order.count > 0);
      CHECK(order.reward > 0);
    }
  }
  CHECK(active == runtime::kOrderSlots);
  // And the deadline landed in the FUTURE rather than staying stale.
  CHECK(chain.world.World().Board().next_refresh > chain.world.World().Now());
}

TEST_CASE("filling an order takes the goods and pays, or refuses") {
  Chain chain;
  chain.world.World().Board().slots[1] = runtime::Order{
      .item = content::kFlour, .count = 2, .reward = 28, .active = true};

  SUBCASE("without the goods, nothing changes") {
    const runtime::EventList& events =
        chain.world.Step(LatchedInput{.fill_slot = 1}, kDt);
    CHECK(Contains<runtime::Refused>(events));
    CHECK(chain.world.World().ThePurse().coin == 0);
    CHECK(chain.world.World().Board().slots[1].active);
  }
  SUBCASE("with them, the slot clears and the purse grows") {
    chain.Barn().Add(content::kFlour, 2);
    const runtime::EventList& events =
        chain.world.Step(LatchedInput{.fill_slot = 1}, kDt);
    CHECK(Contains<runtime::OrderFilled>(events));
    CHECK(chain.world.World().ThePurse().coin == 28);
    CHECK(chain.Barn().Of(content::kFlour) == 0);
    CHECK_FALSE(chain.world.World().Board().slots[1].active);
  }
}

TEST_CASE("land is bought with coin, gets dearer, and runs out") {
  Chain chain(4);
  chain.world.World().TheLand().owned = 2;
  chain.world.World().ThePurse().coin = 1000;

  const U32 first = hearthfield::economy::LandPrice(2);
  chain.world.Step(LatchedInput{.buy_land = true}, kDt);
  CHECK(chain.world.World().TheLand().owned == 3);
  CHECK(chain.world.World().ThePurse().coin == 1000 - first);

  chain.world.Step(LatchedInput{.buy_land = true}, kDt);
  CHECK(chain.world.World().TheLand().owned == 4);

  SUBCASE("and the board is finite") {
    const runtime::EventList& events =
        chain.world.Step(LatchedInput{.buy_land = true}, kDt);
    CHECK(Contains<runtime::Refused>(events));
    CHECK(chain.world.World().TheLand().owned == 4);
  }
  SUBCASE("no coin, no land") {
    Chain poor(9);
    poor.world.World().TheLand().owned = 4;
    poor.world.World().ThePurse().coin = 0;
    const runtime::EventList& events =
        poor.world.Step(LatchedInput{.buy_land = true}, kDt);
    CHECK(Contains<runtime::Refused>(events));
    CHECK(poor.world.World().TheLand().owned == 4);
  }
}

TEST_CASE("LOCKED LAND CANNOT BE SOWN") {
  Chain chain(9);
  chain.world.World().TheLand().owned = 4;
  chain.world.Step(LatchedInput{.tap = true, .hovered = 7}, kDt);
  CHECK(chain.world.World().Plots()[7].state == PlotState::kEmpty);
  chain.world.Step(LatchedInput{.tap = true, .hovered = 1}, kDt);
  CHECK(chain.world.World().Plots()[1].state == PlotState::kGrowing);
}

// ---- the property the whole milestone is measured against -------------------

TEST_CASE("THE OFFLINE EQUIVALENCE STILL HOLDS WITH THE WHOLE CHAIN RUNNING") {
  // Spec risk 5, met head-on. Buildings mid-recipe, crops mid-growth and an
  // order board due to refresh — one step absorbing N ticks must produce a
  // bit-identical world to N+1 ordinary steps.
  //
  // MUTATION PROOF for this case: make the mill take its inputs from the barn
  // when it CONSUMES rather than when it is queued, and this must go red.
  constexpr U32 kAway = 200'000;  // well past a grind and a posting

  const auto build = [](Chain& chain) {
    chain.Barn().Add(content::kWheatItem, 6);
    chain.Barn().Add(content::kCornItem, 4);
    chain.world.Step(LatchedInput{.tap = true, .hovered = 0}, kDt);
    chain.world.Step(LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);
    chain.world.Step(LatchedInput{.queue_recipe = content::kGrindCorn}, kDt);
    chain.world.Step(LatchedInput{.tap = true, .hovered = 1}, kDt);
  };

  Chain jumped;
  Chain stepped;
  build(jumped);
  build(stepped);
  REQUIRE(jumped.world.World().Digest() == stepped.world.World().Digest());

  jumped.world.Step(LatchedInput{.offline_ticks = kAway}, kDt);
  for (U32 i = 0; i < kAway + 1; ++i) {
    stepped.world.Step(LatchedInput{}, kDt);
  }

  CHECK(jumped.world.World().Now() == stepped.world.World().Now());
  CHECK(jumped.world.World().Digest() == stepped.world.World().Digest());
  // And it actually exercised something — an equality between two worlds where
  // nothing happened would pass for the wrong reason.
  CHECK(jumped.Barn().Of(content::kFlour) == 1);
  CHECK(jumped.world.World().Plots()[0].state == PlotState::kReady);
  // 200k ticks is 55 minutes, so the half-hourly board posted once or twice —
  // enough that the RNG advanced, which is the part that has to match.
  CHECK(jumped.world.World().Board().slots[0].active);
}

TEST_CASE("A COMMITTED QUEUE IS WHY: THE MILL NEEDS NOTHING FROM THE BARN") {
  // §3(a) made executable, and THIS is the case that discriminates — not the
  // equivalence test above, which was predicted to catch a consume-time draw
  // and does not (see the plan's §5: both sides of that comparison hold the
  // same barn, so a shared pool they agree about stays invisible).
  //
  // The discriminating move is to EMPTY the barn after queuing. Under
  // commit-at-queue the grind is already paid for and finishes; under a
  // consume-time draw it finds nothing and stalls forever.
  Chain chain;
  chain.Barn().Add(content::kWheatItem, 2);
  chain.world.Step(LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);
  REQUIRE(chain.Mill().queued == 1);
  REQUIRE(chain.Barn().Of(content::kWheatItem) == 0);  // already spent

  // Whatever else happens to the barn, the mill's schedule is its own.
  chain.Barn().counts[content::kWheatItem] = 0;
  chain.world.Step(
      LatchedInput{.offline_ticks = static_cast<U32>(GrindTicks())}, kDt);
  CHECK(chain.Mill().queued == 0);
  CHECK(chain.Barn().Of(content::kFlour) == 1);
}

TEST_CASE(
    "two mills with the same queue advance identically, whatever they see") {
  // The same property from the other direction: a building's future is a
  // function of its own queue and the clock, and of nothing shared.
  Chain rich;
  Chain bare;
  rich.Barn().Add(content::kWheatItem, 2);
  bare.Barn().Add(content::kWheatItem, 2);
  rich.world.Step(LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);
  bare.world.Step(LatchedInput{.queue_recipe = content::kGrindWheat}, kDt);

  rich.Barn().Add(content::kWheatItem, 40);
  rich.Barn().Add(content::kCornItem, 5);

  const auto away = static_cast<U32>(GrindTicks());
  rich.world.Step(LatchedInput{.offline_ticks = away}, kDt);
  bare.world.Step(LatchedInput{.offline_ticks = away}, kDt);

  CHECK(rich.Mill().queued == bare.Mill().queued);
  CHECK(rich.Mill().done_tick == bare.Mill().done_tick);
  CHECK(rich.Barn().Of(content::kFlour) == bare.Barn().Of(content::kFlour));
  CHECK(bare.Barn().Of(content::kFlour) == 1);
}
