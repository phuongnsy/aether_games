// THE COOP — the feature the spec names as the test of risk 5.
//
// Two oracles here and they are not interchangeable. The offline EQUIVALENCE
// test is the obvious one and it is NOT sufficient: both of its runs hold the
// same barn, so a coop that took its wheat at lay time would pass it — the
// exact blindness H3 discovered the hard way with the mill. The one that
// decides the design is the INDEPENDENCE test: commit, EMPTY THE BARN, then run
// the absence.
#include <doctest/doctest.h>

#include "hf/content/animals.hpp"
#include "hf/features/livestock/system.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/runtime/game_world.hpp"

using namespace aether;
using hearthfield::livestock::LivestockSystem;
using hearthfield::runtime::GameWorld;
using hearthfield::runtime::LatchedInput;
using hearthfield::runtime::Tick;
namespace content = hearthfield::content;
namespace runtime = hearthfield::runtime;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;

struct Yard {
  GameWorld world;
  LivestockSystem birds;

  Yard() : world(1, 1) {
    world.AddSystem(birds);
    world.World().TheCoop().animals = content::kFlockSize;
    world.World().TheBarn().capacity = 500;
  }

  void Stock(U32 wheat) {
    (void)world.World().TheBarn().Add(content::kFeedItem, wheat);
  }
  [[nodiscard]] U32 Eggs() const {
    return world.World().TheBarn().Of(content::kLaysItem);
  }
  [[nodiscard]] U32 Wheat() const {
    return world.World().TheBarn().Of(content::kFeedItem);
  }
  void Feed() { world.Step(LatchedInput{.feed_coop = true}, kDt); }
  void Wait(U32 ticks) {
    world.Step(LatchedInput{.offline_ticks = ticks}, kDt);
  }
};

[[nodiscard]] Tick LayInterval() {
  return runtime::TicksFromSeconds(content::kLaySeconds);
}
[[nodiscard]] Tick FedSpan() {
  return runtime::TicksFromSeconds(content::kFedSeconds);
}

template <class T>
[[nodiscard]] bool Has(const runtime::EventList& events) {
  for (const runtime::GameEvent& e : events) {
    if (std::holds_alternative<T>(e)) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("FILLING THE TROUGH DEBITS THE BARN THERE AND THEN") {
  // ADR-0108's rule, applied to the case spec risk 5 names. The wheat leaves
  // the barn at COMMIT time; from here the coop owes it nothing.
  Yard yard;
  yard.Stock(10);
  yard.Feed();
  CHECK(yard.Wheat() == 10 - content::kFeedPerFill);
  CHECK(yard.world.World().TheCoop().fed_until == 1 + FedSpan());
}

TEST_CASE("a fill with no feed in the barn is refused, and takes nothing") {
  Yard yard;
  yard.Stock(content::kFeedPerFill - 1);
  const runtime::EventList& events =
      yard.world.Step(LatchedInput{.feed_coop = true}, kDt);
  CHECK(Has<runtime::Refused>(events));
  CHECK_FALSE(Has<runtime::Fed>(events));
  // ALL-OR-NOTHING: a partly-paid fill would leave the trough owing the barn,
  // which is the coupling this design exists to prevent.
  CHECK(yard.Wheat() == content::kFeedPerFill - 1);
  CHECK(yard.world.World().TheCoop().fed_until == 0);
}

TEST_CASE("THE COOP NEEDS NOTHING FROM THE BARN ONCE IT IS FED") {
  // ***THE ORACLE THAT DECIDES THE DESIGN.*** Fill the trough, then take every
  // grain of wheat away, then be gone for an hour. The eggs must still arrive.
  //
  // A coop that took its feed at LAY time passes every other case in this file
  // — including the equivalence test below, whose two runs both hold the same
  // barn — and fails only here. That is exactly the trap H3 walked into with
  // the mill and had to correct after the fact.
  Yard yard;
  yard.Stock(content::kFeedPerFill);
  yard.Feed();

  // NO `REQUIRE(Wheat() == 0)` HERE, deliberately. That is the COMMIT
  // assertion and it has its own case above; asserting it here too would abort
  // this one before its actual point ran, which is precisely what happened the
  // first time the mutation was tried. A test whose subject is unreachable
  // under the mutation it exists to catch is not the test it claims to be.
  yard.world.World().TheBarn().counts = {};
  yard.Wait(static_cast<U32>(3 * LayInterval()));
  CHECK(yard.Eggs() == 3 * content::kEggsPerLay);
}

TEST_CASE("A HUNGRY COOP LAYS NOTHING") {
  Yard yard;
  yard.Stock(content::kFeedPerFill);
  yard.Feed();
  // Long past the feed running out. What it lays is bounded by the FEED, not
  // by the clock — one `min` in the system, and this is what it buys.
  yard.Wait(static_cast<U32>(FedSpan() + (20 * LayInterval())));
  const auto within_feed = static_cast<U32>(FedSpan() / LayInterval());
  CHECK(yard.Eggs() == within_feed * content::kEggsPerLay);
  CHECK_FALSE(yard.world.World().TheCoop().Fed(yard.world.World().Now()));
}

TEST_CASE("A REFILL AFTER A DROUGHT DOES NOT PAY OUT THE DROUGHT") {
  // `next_lay` sits in the past after a starvation; carrying it forward would
  // hand the player a week of eggs for one scoop of wheat (H5 plan §3b).
  Yard yard;
  yard.Stock(2 * content::kFeedPerFill);
  yard.Feed();
  yard.Wait(static_cast<U32>(FedSpan() + (50 * LayInterval())));
  const U32 before = yard.Eggs();

  yard.Feed();
  CHECK(yard.Eggs() == before);  // nothing on the fill step itself
  // One interval later, exactly one lay — not fifty.
  yard.Wait(static_cast<U32>(LayInterval()));
  CHECK(yard.Eggs() == before + content::kEggsPerLay);
}

TEST_CASE("topping up an unfinished trough EXTENDS it rather than restarting") {
  Yard yard;
  yard.Stock(3 * content::kFeedPerFill);
  yard.Feed();
  const Tick first = yard.world.World().TheCoop().fed_until;
  yard.Wait(60);
  yard.Feed();
  CHECK(yard.world.World().TheCoop().fed_until == first + FedSpan());
}

TEST_CASE("a full barn loses the eggs but does NOT stall the timer") {
  // The same bargain a harvest into a full barn already makes. Stalling would
  // make the outcome depend on WHEN the barn was emptied, which is precisely
  // the cross-entity coupling that has no closed form.
  Yard yard;
  yard.Stock(content::kFeedPerFill);
  yard.Feed();
  yard.world.World().TheBarn().capacity = 0;
  // WITHIN the feed window, so the only thing that could stop the timer is the
  // full barn — which is what is being asserted.
  yard.Wait(static_cast<U32>(3 * LayInterval()));
  CHECK(yard.Eggs() == 0);
  // The timer moved anyway: three intervals were consumed, not banked.
  CHECK(yard.world.World().TheCoop().next_lay ==
        1 + LayInterval() + (3 * LayInterval()));
}

TEST_CASE("ONE JUMPED STEP EQUALS N STEPPED ONES, WITH THE COOP RUNNING") {
  // Necessary and NOT sufficient — see the file header. Its value is catching a
  // catch-up that computes a different COUNT or leaves `next_lay` somewhere
  // else, which integer division gets wrong at exactly the boundaries.
  // Long enough to cover several lays and deliberately NOT a multiple of the
  // interval: integer division is exactly what gets boundaries wrong.
  const auto kAway = static_cast<U32>((2 * LayInterval()) + 137);
  Yard jumped;
  Yard stepped;
  jumped.Stock(20);
  stepped.Stock(20);
  jumped.Feed();
  stepped.Feed();
  REQUIRE(jumped.world.World().Digest() == stepped.world.World().Digest());

  jumped.Wait(kAway);
  for (U32 i = 0; i < kAway + 1; ++i) {
    stepped.world.Step(LatchedInput{}, kDt);
  }
  CHECK(jumped.world.World().Digest() == stepped.world.World().Digest());
  CHECK(jumped.Eggs() > 0);  // or the equality above is vacuous
}

TEST_CASE("the coop announces hunger ONCE, including across an absence") {
  Yard yard;
  yard.Stock(content::kFeedPerFill);
  yard.Feed();
  // The step that crosses the deadline — here, the one that absorbs the gap.
  const runtime::EventList& crossing = yard.world.Step(
      LatchedInput{.offline_ticks = static_cast<U32>(FedSpan() + 10)}, kDt);
  CHECK(Has<runtime::CoopHungry>(crossing));
  const runtime::EventList& after = yard.world.Step(LatchedInput{}, kDt);
  CHECK_FALSE(Has<runtime::CoopHungry>(after));
}

TEST_CASE("a coop with no birds is a building, not a bug") {
  Yard yard;
  yard.world.World().TheCoop().animals = 0;
  yard.Stock(10);
  const runtime::EventList& events =
      yard.world.Step(LatchedInput{.feed_coop = true}, kDt);
  CHECK(Has<runtime::Refused>(events));
  CHECK(yard.Wheat() == 10);
  yard.Wait(static_cast<U32>(10 * LayInterval()));
  CHECK(yard.Eggs() == 0);
}
