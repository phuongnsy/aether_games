// Lantern's sim, headless. No device, no render library — if this file ever
// needs one, a layer boundary has been crossed (games/CLAUDE.md law 4).
#include "lantern/features/lanterns/lanterns.hpp"

#include <doctest/doctest.h>

using namespace aether;
using lantern::lanterns::Lanterns;

TEST_CASE("lanterns: the nearest UNLIT one within reach is the one you light") {
  Lanterns set;
  set.Add(Vec3{0.0f, 0.0f, 0.0f});
  set.Add(Vec3{1.0f, 0.0f, 0.0f});

  // Standing between them, closer to the second.
  const Usize pick = set.NearestUnlit(Vec3{0.7f, 0.0f, 0.0f}, 2.0f, 4.0f);
  CHECK(pick == 1);
  CHECK(set.Light(pick));
  // Already lit: the same spot now finds the OTHER one, not nothing and not
  // the same one again.
  CHECK(set.NearestUnlit(Vec3{0.7f, 0.0f, 0.0f}, 2.0f, 4.0f) == 0);
}

TEST_CASE("lanterns: reach is a COLUMN — under counts, above does not") {
  // A lantern hangs over its platform, so standing beneath it must count even
  // when the straight-line distance is large. And one BELOW you is a lantern
  // you climbed past: reaching down to it would undo the ascent it marks.
  Lanterns set;
  set.Add(Vec3{0.0f, 3.4f, 0.0f});   // overhead
  set.Add(Vec3{0.0f, -3.0f, 0.0f});  // already passed
  CHECK(set.NearestUnlit(Vec3{0.0f, 0.4f, 0.0f}, 2.2f, 3.8f) == 0);
  // Standing above the first: neither is reachable.
  CHECK(set.NearestUnlit(Vec3{0.0f, 5.0f, 0.0f}, 2.2f, 3.8f) ==
        static_cast<Usize>(-1));
}

TEST_CASE("lanterns: out of reach is out of reach") {
  Lanterns set;
  set.Add(Vec3{10.0f, 0.0f, 0.0f});
  CHECK(set.NearestUnlit(Vec3{}, 2.0f, 4.0f) == static_cast<Usize>(-1));
  // And lighting a nonexistent index is a no-op rather than a crash, because
  // the caller passes NearestUnlit's answer straight through.
  CHECK_FALSE(set.Light(static_cast<Usize>(-1)));
}

TEST_CASE("lanterns: the checkpoint is the LAST one lit, not the nearest") {
  // A player who climbs past a lantern without lighting it has chosen the
  // risk; quietly rescuing them to it would erase the choice.
  Lanterns set;
  set.Add(Vec3{0.0f, 0.0f, 0.0f});
  set.Add(Vec3{0.0f, 10.0f, 0.0f});
  CHECK_FALSE(set.HasCheckpoint());

  REQUIRE(set.Light(0));
  CHECK(set.HasCheckpoint());
  CHECK(set.Checkpoint().y == doctest::Approx(0.0f));

  REQUIRE(set.Light(1));
  CHECK(set.Checkpoint().y == doctest::Approx(10.0f));
}

TEST_CASE("lanterns: lighting one twice does not double the count") {
  Lanterns set;
  set.Add(Vec3{});
  CHECK(set.Light(0));
  CHECK_FALSE(set.Light(0));
  CHECK(set.LitCount() == 1);
}
