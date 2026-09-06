// The weather, which is a function and must stay one.
//
// Every case here is really the same assertion from a different side: nothing
// about the sky is STATE. That is what makes an absence free and what keeps the
// world RNG — whose value is saved and digested — out of the presentation path.
#include "hf/runtime/weather.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

#include "aether/audio/spatial.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/runtime/game_world.hpp"
#include "hf/view/farm_audio.hpp"

using namespace aether;
using hearthfield::runtime::GameWorld;
using hearthfield::runtime::LatchedInput;
using hearthfield::runtime::RainingAt;
using hearthfield::runtime::Tick;
namespace runtime = hearthfield::runtime;
namespace view = hearthfield::view;
namespace audio = aether::audio;

namespace {
constexpr F32 kDt = 1.0f / 60.0f;
constexpr Tick kSpell = 1200 * 60;  // kSpellSeconds at kTicksPerSecond
}  // namespace

// A CONSTEXPR assertion, which is the strongest available statement that this
// depends on nothing: if it needed state, a clock or an allocation it could not
// be evaluated at compile time at all.
static_assert(RainingAt(0) == RainingAt(0));
static_assert(RainingAt(7) == RainingAt(kSpell - 1));

TEST_CASE("THE ANSWER FOR A TICK IS THE SAME WHENEVER IT IS ASKED") {
  // Including for ticks a month ahead of anything the sim has stepped through,
  // which is the property that makes an absence cost nothing.
  constexpr Tick kMonth = 30ULL * 24 * 60 * 60 * 60;
  for (Tick t = 0; t < 40; ++t) {
    const Tick far = kMonth + (t * kSpell);
    CHECK(RainingAt(far) == RainingAt(far));
    CHECK(RainingAt(far) == RainingAt(far + kSpell - 1));
  }
}

TEST_CASE("a spell is uniform, and its neighbours are not correlated") {
  // Uniform within: the sky must not flicker inside one spell.
  const bool wet = RainingAt(0);
  for (Tick t = 0; t < kSpell; t += kSpell / 37) {
    CHECK(RainingAt(t) == wet);
  }

  // AND IT IS NOT DEGENERATE. A hash that returned a constant, or one whose
  // period aligned with the spell length, would pass every case above — this is
  // the one that notices. A day is 72 spells; both kinds must appear.
  U32 wet_spells = 0;
  constexpr U32 kSpellsPerDay = 72;
  for (U32 s = 0; s < kSpellsPerDay; ++s) {
    if (RainingAt(s * kSpell)) {
      ++wet_spells;
    }
  }
  CHECK(wet_spells > 0);
  CHECK(wet_spells < kSpellsPerDay);
}

TEST_CASE("STEPPING A RAINY WORLD DOES NOT TOUCH THE WORLD RNG") {
  // The failure this guards is the nastiest shape available: rolling for
  // weather would shift the order board's numbers and break every digest
  // comparison in the project — but only in a build that RENDERS, because a
  // headless test never draws a raindrop. So it is asserted headless, here.
  GameWorld world(/*seed=*/9, /*plot_count=*/4);
  hearthfield::plots::PlotsSystem plots;
  world.AddSystem(plots);

  // Land on a tick that is definitely wet, or the case proves nothing.
  Tick wet_at = 0;
  while (!RainingAt(wet_at) && wet_at < 200 * kSpell) {
    wet_at += kSpell;
  }
  REQUIRE(RainingAt(wet_at));

  world.Step(LatchedInput{.offline_ticks = static_cast<U32>(wet_at)}, kDt);
  REQUIRE(world.Snapshot().raining);
  const U64 before = world.World().Random().State();
  for (int i = 0; i < 30; ++i) {
    world.Step(LatchedInput{}, kDt);
  }
  CHECK(world.World().Random().State() == before);
}

TEST_CASE("the snapshot carries the sky, so view never has to ask twice") {
  GameWorld world(/*seed=*/1, /*plot_count=*/1);
  world.Step(LatchedInput{}, kDt);
  CHECK(world.Snapshot().raining == RainingAt(world.World().Now()));
}

// ---- the listener, and where a parallel projection puts it -----------------

TEST_CASE("THE LISTENER IS AT WHAT THE CAMERA LOOKS AT, NOT AT THE CAMERA") {
  // ADR-0081's second silent-failure mode. Under kOrthographic3D the eye's
  // distance changes nothing you can SEE — the farm's world file puts it 40 m
  // back and any other number would render identically — so a listener at the
  // eye measures every sound on the farm as 40 m away: uniformly quiet, barely
  // panned, and indistinguishable from a volume bug.
  const Vec3 board{0.0f, 0.0f, 0.0f};
  const audio::Listener at_target = view::ListenerFor(board, 0.0f, 0.6f);
  CHECK(at_target.position.x == doctest::Approx(0.0f));
  CHECK(at_target.position.y == doctest::Approx(0.0f));
  CHECK(at_target.position.z == doctest::Approx(0.0f));

  // The mill, one board-width away, is genuinely nearer than the falloff max —
  // which is the property the eye position would destroy.
  const audio::SpatialSource mill{.position = Vec3{6.0f, 0.0f, 0.0f},
                                  .falloff_min = view::kMillFalloffMin,
                                  .falloff_max = view::kMillFalloffMax};
  CHECK(audio::Audible(at_target, mill));
  CHECK(audio::Spatialize(at_target, mill).gain > 0.0f);

  // AND THE COMPARISON IS THE POINT. The farm's world file puts the eye 40 m
  // back — past the mill's falloff max — so a listener there hears NOTHING at
  // all, from a camera that renders exactly the same picture. Not "quieter":
  // silent.
  const audio::Listener at_eye{.position = Vec3{0.0f, 0.0f, 40.0f}};
  CHECK_FALSE(audio::Audible(at_eye, mill));
  CHECK(audio::Spatialize(at_eye, mill).gain == doctest::Approx(0.0f));
}

TEST_CASE("TURNING THE BOARD MOVES THE MILL TO THE OTHER EAR") {
  // Check 8's audible half, as arithmetic. The mill sits east of the board and
  // the coop west, on purpose: with both on one side a turn would move them
  // together and a sign error in the pan would be invisible.
  const audio::SpatialSource mill{.position = Vec3{6.0f, 0.0f, 0.0f},
                                  .falloff_min = view::kMillFalloffMin,
                                  .falloff_max = view::kMillFalloffMax};
  constexpr F32 kHalfTurn = std::numbers::pi_v<float>;

  const F32 facing =
      audio::Spatialize(view::ListenerFor(Vec3{}, 0.0f, 0.35f), mill).pan;
  const F32 turned =
      audio::Spatialize(view::ListenerFor(Vec3{}, kHalfTurn, 0.35f), mill).pan;
  CHECK(facing != doctest::Approx(0.0f));
  CHECK(facing * turned < 0.0f);  // opposite ears
  CHECK(facing == doctest::Approx(-turned).epsilon(0.01));
}

TEST_CASE("the listener's orientation is the camera's, pitch included") {
  // Not a tautology: the derivation is COPIED from OrbitComponent rather than
  // read off the node, so the thing worth asserting is that pitching the camera
  // does not spin the sound field sideways. Elevation must not pan.
  const audio::SpatialSource overhead{.position = Vec3{0.0f, 8.0f, -0.01f},
                                      .falloff_min = 1.0f,
                                      .falloff_max = 40.0f};
  for (const F32 pitch : {0.0f, 0.35f, 1.2f}) {
    const F32 pan =
        audio::Spatialize(view::ListenerFor(Vec3{}, 0.0f, pitch), overhead).pan;
    CHECK(std::abs(pan) < 0.2f);
  }
}
