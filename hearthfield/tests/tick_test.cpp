// The farm clock and the crop catalogue: the two things every later milestone
// does arithmetic against.
#include "hf/runtime/tick.hpp"

#include <doctest/doctest.h>

#include "hf/content/crops.hpp"
#include "hf/runtime/rng.hpp"
#include "hf/runtime/world_view.hpp"

using namespace aether;
using hearthfield::runtime::Rng;
using hearthfield::runtime::Tick;
using hearthfield::runtime::TicksFromSeconds;
using hearthfield::runtime::WorldView;
namespace content = hearthfield::content;

TEST_CASE("durations convert through the ONE site that knows the step rate") {
  CHECK(TicksFromSeconds(1.0) == hearthfield::runtime::kTicksPerSecond);
  CHECK(TicksFromSeconds(600.0) == 36000);  // wheat: ten minutes at 60 Hz
  CHECK(hearthfield::runtime::SecondsFromTicks(36000) ==
        doctest::Approx(600.0));

  SUBCASE("a duration never rounds down to nothing") {
    // A crop ready the tick it was sown is the failure this floor prevents;
    // content's static_assert refuses a zero, and this covers the rounding.
    CHECK(TicksFromSeconds(0.001) == 1);
    CHECK(TicksFromSeconds(0.0) == 1);
    CHECK(TicksFromSeconds(-5.0) == 1);
  }
}

TEST_CASE("every catalogue entry survives the trip to ticks") {
  for (const content::Crop& crop : content::kCrops) {
    CAPTURE(crop.name);
    CHECK(TicksFromSeconds(crop.grow_seconds) > 0);
    CHECK(crop.yield > 0);
  }
  CHECK(content::CropExists(content::kWheat));
  CHECK_FALSE(content::CropExists(
      static_cast<content::CropId>(content::kCrops.size())));
}

TEST_CASE("THE TICK COUNTER SURVIVES MORE THAN A U32 OF WALL CLOCK") {
  // The reason Tick is U64. At 60 Hz a U32 wraps after 2.27 YEARS of calendar
  // time — and this is the first game here whose clock counts time spent not
  // playing, so that ceiling is reachable by a farm nobody even opens.
  WorldView world(1, 1);
  constexpr Tick kBeyondU32 = 5'000'000'000ULL;
  world.AdvanceBy(kBeyondU32);
  CHECK(world.Now() == kBeyondU32);
  CHECK(world.Now() > 0xFFFFFFFFULL);
}

TEST_CASE("the RNG round-trips through its state, because a save must") {
  // Spec §4.4: a farm reloaded with a fresh RNG is a different farm. This is
  // the property that made splitmix64 the choice over std::mt19937 — the whole
  // generator is one U64, so H2 can write it without a serializer.
  Rng live(12345);
  for (int i = 0; i < 7; ++i) {
    (void)live.NextU64();
  }
  const U64 saved = live.State();

  Rng restored(0);
  restored.SetState(saved);
  for (int i = 0; i < 5; ++i) {
    CHECK(restored.NextU64() == live.NextU64());
  }
}

TEST_CASE(
    "the digest discriminates — a hash that always matches proves nothing") {
  WorldView a(1, 2);
  WorldView b(1, 2);
  REQUIRE(a.Digest() == b.Digest());

  SUBCASE("one plot's state") {
    b.Plots()[1].state = hearthfield::runtime::PlotState::kGrowing;
    CHECK(a.Digest() != b.Digest());
  }
  SUBCASE("one tick") {
    b.AdvanceBy(1);
    CHECK(a.Digest() != b.Digest());
  }
  SUBCASE("one draw from the RNG") {
    (void)b.Random().NextU64();
    CHECK(a.Digest() != b.Digest());
  }
  SUBCASE("a different seed") {
    WorldView c(2, 2);
    CHECK(a.Digest() != c.Digest());
  }
}
