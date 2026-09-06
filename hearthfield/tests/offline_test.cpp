// THE OFFLINE MODEL, made executable.
//
// The spec's §4.3 claims an absence is arithmetic rather than simulation, and
// its risk 5 says that holds only while no feature couples across entities.
// Both are claims about behaviour, so both are testable, and the test below is
// the one that will fail the day the claim stops being true — which is exactly
// when the design review of some future feature needs to hear about it.
#include <doctest/doctest.h>

#include "hf/content/crops.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/runtime/game_world.hpp"

using namespace aether;
using hearthfield::plots::PlotsSystem;
using hearthfield::runtime::GameWorld;
using hearthfield::runtime::LatchedInput;
using hearthfield::runtime::PlotState;
using hearthfield::runtime::Tick;
namespace content = hearthfield::content;
namespace runtime = hearthfield::runtime;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;

// A world with `plots` plots, each sown on the first step.
struct Farm {
  GameWorld world;
  PlotsSystem plots;

  explicit Farm(Usize count) : world(1, count) { world.AddSystem(plots); }

  void SowAll() {
    for (Usize i = 0; i < world.World().Plots().size(); ++i) {
      world.Step(
          LatchedInput{.tap = true, .hovered = static_cast<runtime::PlotId>(i)},
          kDt);
    }
  }
};

}  // namespace

TEST_CASE("ONE STEP THAT ABSORBS N TICKS EQUALS N+1 ORDINARY STEPS") {
  // The headline property. If these two worlds ever differ, the closed-form
  // catch-up is wrong and eight hours of absence has become a real simulation
  // with no cheap answer.
  constexpr U32 kAway = 5000;

  Farm jumped(3);
  Farm stepped(3);
  jumped.SowAll();
  stepped.SowAll();
  REQUIRE(jumped.world.World().Digest() == stepped.world.World().Digest());

  jumped.world.Step(LatchedInput{.offline_ticks = kAway}, kDt);
  for (U32 i = 0; i < kAway + 1; ++i) {
    stepped.world.Step(LatchedInput{}, kDt);
  }

  CHECK(jumped.world.World().Now() == stepped.world.World().Now());
  CHECK(jumped.world.World().Digest() == stepped.world.World().Digest());
}

TEST_CASE("the equivalence holds ACROSS the ripening boundary") {
  // The interesting case: the absence must be long enough that the crop
  // becomes ready DURING the gap, so the jumped world has to reach a
  // conclusion the stepped one arrives at gradually.
  const Tick grow = runtime::TicksFromSeconds(
      content::CropById(content::kWheat).grow_seconds);
  const auto away = static_cast<U32>(grow + 500);

  Farm jumped(2);
  Farm stepped(2);
  jumped.SowAll();
  stepped.SowAll();

  jumped.world.Step(LatchedInput{.offline_ticks = away}, kDt);
  for (U32 i = 0; i < away + 1; ++i) {
    stepped.world.Step(LatchedInput{}, kDt);
  }

  REQUIRE(jumped.world.World().Plots()[0].state == PlotState::kReady);
  CHECK(jumped.world.World().Digest() == stepped.world.World().Digest());
}

TEST_CASE("an absence announces each ripened crop once, not once per tick") {
  // The event side of the same claim. A catch-up that replayed the gap step by
  // step would be correct and unusable: it would hand view a million events.
  Farm farm(4);
  farm.SowAll();
  const Tick grow = runtime::TicksFromSeconds(
      content::CropById(content::kWheat).grow_seconds);

  const runtime::EventList& events = farm.world.Step(
      LatchedInput{.offline_ticks = static_cast<U32>(grow * 10)}, kDt);
  CHECK(events.size() == 4);
}

TEST_CASE("EIGHT HOURS AWAY COSTS ONE STEP") {
  // 1.7 million fixed steps at 60 Hz, absorbed by one comparison per plot.
  // Not a benchmark — a demonstration that the O(plots) claim is structural:
  // this case would take minutes if the catch-up were stepped.
  constexpr U32 kEightHours = 8 * 60 * 60 * 60;
  Farm farm(64);
  farm.SowAll();
  farm.world.Step(LatchedInput{.offline_ticks = kEightHours}, kDt);

  for (const runtime::Plot& plot : farm.world.World().Plots()) {
    CHECK(plot.state == PlotState::kReady);
  }
  CHECK(farm.world.Snapshot().ready_count == 64);
}
