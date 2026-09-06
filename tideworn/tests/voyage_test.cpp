// The voyage schedule is the game's weather authority, so its shape is pinned:
// wind stays inside the ADR-0057 range, bands fire exactly once per crossing,
// and the fixed step is deterministic (the replay pillar rests on it).
#include <doctest/doctest.h>

#include <vector>

#include "tideworn/content/content.hpp"
#include "tideworn/features/voyage/system.hpp"
#include "tideworn/features/voyage/tuning.hpp"
#include "tideworn/runtime/game_world.hpp"

using namespace aether;
using namespace tideworn;

TEST_CASE("voyage: wind follows the schedule and stays in the decided range") {
  // ADR-0057 replaced the 9 m/s cap with a 3-25 m/s dynamic range; a schedule
  // that wanders outside it silently reopens the calibration question.
  for (int i = 0; i <= 1200; ++i) {
    const F32 wind = voyage::WindAt(static_cast<F32>(i));
    CHECK(wind >= 3.0f);
    CHECK(wind <= 25.0f);
  }
  // The keyframes themselves are hit exactly.
  for (const content::WindKey& key : content::kWindSchedule) {
    CHECK(voyage::WindAt(key.at_s) == doctest::Approx(key.wind_mps));
  }
  // And the schedule LOOPS: one period later is the same sea.
  const F32 period = content::kWindSchedule.back().at_s;
  CHECK(voyage::WindAt(37.0f) ==
        doctest::Approx(voyage::WindAt(37.0f + period)));
}

TEST_CASE("voyage: the schedule actually visits calm and storm") {
  // A voyage that never meets a storm is the old 9 m/s brief wearing a new
  // name; assert the drama is authored, not hoped for.
  bool calm = false;
  bool storm = false;
  for (int i = 0; i <= 600; ++i) {
    const voyage::Band band =
        voyage::BandFor(voyage::WindAt(static_cast<F32>(i)));
    calm = calm || band == voyage::Band::kCalm;
    storm = storm || band == voyage::Band::kStorm;
  }
  CHECK(calm);
  CHECK(storm);
}

TEST_CASE("voyage: band transitions fire exactly once per crossing") {
  voyage::VoyageState state;
  std::vector<voyage::BandChanged> events;
  int transitions = 0;
  voyage::Band previous = state.band;
  for (int i = 0; i < 600 * 60; ++i) {
    events.clear();
    voyage::Step(state, 1.0f / 60.0f, events);
    // An event happens iff the band moved this step — no repeats, no misses.
    if (state.band != previous) {
      REQUIRE(events.size() == 1);
      CHECK(events.front().from == previous);
      CHECK(events.front().to == state.band);
      ++transitions;
    } else {
      CHECK(events.empty());
    }
    previous = state.band;
  }
  // One full cycle crosses calm->fresh->gale->storm and back: 6 transitions.
  CHECK(transitions == 6);
}

TEST_CASE("game world: two runs of the same steps are identical") {
  // The determinism pillar (games/CLAUDE.md): a pure function of
  // (state, input, dt) — this is what input-script replay rests on.
  runtime::GameWorld a(7);
  runtime::GameWorld b(7);
  for (int i = 0; i < 3000; ++i) {
    a.Step(runtime::LatchedInput{}, 1.0f / 60.0f);
    b.Step(runtime::LatchedInput{}, 1.0f / 60.0f);
  }
  const runtime::ViewSnapshot sa = a.Snapshot();
  const runtime::ViewSnapshot sb = b.Snapshot();
  CHECK(sa.clock_s == sb.clock_s);
  CHECK(sa.wind_speed == sb.wind_speed);
  CHECK(sa.boat_pos.x == sb.boat_pos.x);
  CHECK(sa.boat_pos.y == sb.boat_pos.y);
  CHECK(sa.band == sb.band);
}

TEST_CASE("game world: events buffer until drained") {
  runtime::GameWorld world;
  // Step across the first band boundary (calm -> fresh at 8 m/s).
  for (int i = 0; i < 200 * 60; ++i) {
    world.Step(runtime::LatchedInput{}, 1.0f / 60.0f);
  }
  CHECK(!world.Events().empty());
  world.ClearEvents();
  CHECK(world.Events().empty());
}
