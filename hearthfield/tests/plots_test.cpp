// Sow, grow, harvest — the loop's first two verbs, in the step.
#include <doctest/doctest.h>

#include <variant>

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

// The wheat timer in ticks — read from the catalogue rather than written as a
// literal, so retuning the crop cannot silently invalidate these cases.
const Tick kWheatTicks =
    runtime::TicksFromSeconds(content::CropById(content::kWheat).grow_seconds);

[[nodiscard]] LatchedInput TapOn(runtime::PlotId plot) {
  return LatchedInput{.tap = true, .hovered = plot};
}

template <class E>
[[nodiscard]] bool Contains(const runtime::EventList& events) {
  for (const runtime::GameEvent& event : events) {
    if (std::holds_alternative<E>(event)) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("a tap on an empty plot sows it") {
  GameWorld world(1, 1);
  PlotsSystem plots;
  world.AddSystem(plots);

  const runtime::EventList& events = world.Step(TapOn(0), kDt);
  CHECK(Contains<runtime::Sown>(events));

  const runtime::Plot& plot = world.World().Plots()[0];
  CHECK(plot.state == PlotState::kGrowing);
  CHECK(plot.crop == content::kWheat);
  CHECK(plot.ready_tick == plot.sown_tick + kWheatTicks);
}

TEST_CASE("A CROP IS READY AT ITS ready_tick AND NOT ONE TICK BEFORE") {
  // The off-by-one that would make every crop in the game early or late, and
  // the kind of thing only an exact test catches — a ten-minute timer looks
  // right either way.
  GameWorld world(1, 1);
  PlotsSystem plots;
  world.AddSystem(plots);
  world.Step(TapOn(0), kDt);
  const Tick ready_at = world.World().Plots()[0].ready_tick;
  // GUARDED, because the loop below counts on an UNSIGNED tick: if the sow
  // silently stops working, `ready_at - 1` wraps and the test spins forever
  // instead of failing. That happened once — land ownership arrived and every
  // plot defaulted to locked — and a hang is far harder to diagnose than a red.
  REQUIRE(ready_at > 1);

  // Step to exactly one tick short.
  while (world.World().Now() < ready_at - 1) {
    world.Step(LatchedInput{}, kDt);
  }
  REQUIRE(world.World().Now() == ready_at - 1);
  CHECK(world.World().Plots()[0].state == PlotState::kGrowing);

  const runtime::EventList& events = world.Step(LatchedInput{}, kDt);
  REQUIRE(world.World().Now() == ready_at);
  CHECK(world.World().Plots()[0].state == PlotState::kReady);
  CHECK(Contains<runtime::CropReady>(events));
}

TEST_CASE("CropReady fires once, not once per tick after ripening") {
  GameWorld world(1, 1);
  PlotsSystem plots;
  world.AddSystem(plots);
  world.Step(TapOn(0), kDt);
  REQUIRE(world.World().Plots()[0].ready_tick > 1);  // see the case above

  int announcements = 0;
  while (world.World().Now() < world.World().Plots()[0].ready_tick + 5) {
    if (Contains<runtime::CropReady>(world.Step(LatchedInput{}, kDt))) {
      ++announcements;
    }
  }
  CHECK(announcements == 1);
}

TEST_CASE("a tap on a ready plot harvests it, and the plot returns to empty") {
  GameWorld world(1, 1);
  PlotsSystem plots;
  world.AddSystem(plots);
  world.Step(TapOn(0), kDt);
  // One absence long enough to ripen it, which is also the shortest way to
  // write "much later" in this game.
  world.Step(LatchedInput{.offline_ticks = static_cast<U32>(kWheatTicks)}, kDt);
  REQUIRE(world.World().Plots()[0].state == PlotState::kReady);

  const runtime::EventList& events = world.Step(TapOn(0), kDt);
  REQUIRE(Contains<runtime::Harvested>(events));
  const auto& harvested = std::get<runtime::Harvested>(events.front());
  CHECK(harvested.plot == 0);
  CHECK(harvested.crop == content::kWheat);
  CHECK(harvested.amount == content::CropById(content::kWheat).yield);
  CHECK(world.World().Plots()[0].state == PlotState::kEmpty);
}

TEST_CASE("a tap that lands on no plot does nothing") {
  GameWorld world(1, 2);
  PlotsSystem plots;
  world.AddSystem(plots);
  const U64 before = world.World().Digest();

  world.Step(TapOn(runtime::kNoPlot), kDt);
  world.Step(TapOn(99), kDt);

  // Only the clock moved: no plot changed, so nothing but `now` differs.
  CHECK(world.World().Plots()[0].state == PlotState::kEmpty);
  CHECK(world.World().Plots()[1].state == PlotState::kEmpty);
  CHECK(world.World().Digest() != before);  // the tick DID advance
}

TEST_CASE("the snapshot reports growth without storing it") {
  GameWorld world(1, 1);
  PlotsSystem plots;
  world.AddSystem(plots);
  world.Step(TapOn(0), kDt);
  CHECK(world.Snapshot().plots[0].growth == doctest::Approx(0.0f));
  CHECK(world.Snapshot().ready_count == 0);

  world.Step(LatchedInput{.offline_ticks = static_cast<U32>(kWheatTicks / 2)},
             kDt);
  CHECK(world.Snapshot().plots[0].growth ==
        doctest::Approx(0.5f).epsilon(0.01));

  world.Step(LatchedInput{.offline_ticks = static_cast<U32>(kWheatTicks)}, kDt);
  CHECK(world.Snapshot().plots[0].growth == doctest::Approx(1.0f));
  CHECK(world.Snapshot().ready_count == 1);
  CHECK(world.Snapshot().tick == world.World().Now());
}
