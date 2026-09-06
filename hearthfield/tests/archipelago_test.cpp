// Owning islands: the unlock verb, and what a save's two fields are allowed to
// mean once this build has looked at them.
//
// Pure and headless — no scene, no device, no file. The verb has no button yet
// (sky world s4 defers the shop entry to s5, when there is somewhere to go), so
// these tests are the ONLY thing exercising it, which is the argument for
// covering all four of its answers rather than the happy one.
#include "hf/runtime/archipelago.hpp"

#include <doctest/doctest.h>

using namespace aether;
using namespace hearthfield;

TEST_CASE("A FRESH ARCHIPELAGO OWNS THE HUB AND NOTHING ELSE") {
  const runtime::Archipelago isles;
  CHECK(runtime::IsUnlocked(isles, content::kHub));
  CHECK_FALSE(runtime::IsUnlocked(isles, content::kNearIsle));
  CHECK_FALSE(runtime::IsUnlocked(isles, content::kFarIsle));
  CHECK(isles.current == content::kHub);
}

TEST_CASE("AN ISLAND IS BOUGHT ONCE, AND THE PURSE PAYS EXACTLY ITS PRICE") {
  runtime::Archipelago isles;
  runtime::Purse purse;
  const U32 price = content::kIslands[content::kNearIsle].unlock_coins;
  REQUIRE(price > 0);

  SUBCASE("too little coin buys nothing, and SPENDS nothing") {
    purse.coin = price - 1;
    CHECK(runtime::UnlockIsland(isles, purse, content::kNearIsle) ==
          runtime::UnlockResult::kCannotAfford);
    // THE HALF-PAID FAILURE is what this line exists to prevent: a refusal that
    // still debited would be invisible until a player noticed the shortfall.
    CHECK(purse.coin == price - 1);
    CHECK_FALSE(runtime::IsUnlocked(isles, content::kNearIsle));
  }

  SUBCASE("exactly its price is enough") {
    purse.coin = price;
    CHECK(runtime::UnlockIsland(isles, purse, content::kNearIsle) ==
          runtime::UnlockResult::kBought);
    CHECK(purse.coin == 0);
    CHECK(runtime::IsUnlocked(isles, content::kNearIsle));
    // AND THE OTHER SATELLITE IS UNTOUCHED — a bitmask makes it easy to set the
    // wrong bit, and easy for that to look right in a one-island test.
    CHECK_FALSE(runtime::IsUnlocked(isles, content::kFarIsle));
  }

  SUBCASE("buying it twice is refused and charges nothing the second time") {
    purse.coin = price * 3;
    REQUIRE(runtime::UnlockIsland(isles, purse, content::kNearIsle) ==
            runtime::UnlockResult::kBought);
    const U32 after_first = purse.coin;
    CHECK(runtime::UnlockIsland(isles, purse, content::kNearIsle) ==
          runtime::UnlockResult::kAlreadyOwned);
    CHECK(purse.coin == after_first);
  }

  SUBCASE("an island that does not exist is named as such, not merely false") {
    purse.coin = 100000;
    CHECK(runtime::UnlockIsland(
              isles, purse,
              static_cast<content::IslandId>(content::kIslands.size())) ==
          runtime::UnlockResult::kNoSuchIsland);
    CHECK(purse.coin == 100000);
  }
}

TEST_CASE("THE HUB IS FREE, WHICH IS WHY A NEW FARM HAS ONE") {
  CHECK(content::kIslands[content::kHub].unlock_coins == 0);
}

TEST_CASE("A SAVE'S ARCHIPELAGO IS NOT TRUSTED AS WRITTEN") {
  // Both fields can name something this build does not have — a file from a
  // later version, or one whose island table shrank. Neither may be believed:
  // a bit past the table's end would index off it, and a `current` that is
  // locked or absent would strand the player somewhere they cannot be.
  SUBCASE("bits naming islands this build lacks are dropped") {
    const runtime::Archipelago isles =
        runtime::IslandsFromSave(0xFFFFFFFFu, content::kHub);
    const U32 present = static_cast<U32>((1u << content::kIslands.size()) - 1u);
    CHECK(isles.unlocked == present);
  }

  SUBCASE("the hub is restored even by a file that says otherwise") {
    // A farm you cannot reach is not a recoverable state, so this is an
    // invariant rather than a starting value.
    const runtime::Archipelago isles = runtime::IslandsFromSave(0u, 0u);
    CHECK(runtime::IsUnlocked(isles, content::kHub));
    CHECK(isles.current == content::kHub);
  }

  SUBCASE("standing on a LOCKED island sends the player home") {
    const runtime::Archipelago isles =
        runtime::IslandsFromSave(1u << content::kHub, content::kFarIsle);
    CHECK(isles.current == content::kHub);
  }

  SUBCASE("standing on an ABSENT island sends the player home") {
    const runtime::Archipelago isles = runtime::IslandsFromSave(
        0xFFFFFFFFu, static_cast<U32>(content::kIslands.size()));
    CHECK(isles.current == content::kHub);
  }

  SUBCASE("a satellite the file legitimately owns is kept, and stood upon") {
    const runtime::Archipelago isles = runtime::IslandsFromSave(
        (1u << content::kHub) | (1u << content::kFarIsle), content::kFarIsle);
    CHECK(runtime::IsUnlocked(isles, content::kFarIsle));
    CHECK_FALSE(runtime::IsUnlocked(isles, content::kNearIsle));
    CHECK(isles.current == content::kFarIsle);
  }
}

// --- the sim verbs (sky world s7) --------------------------------------------
//
// Buying an island and crossing to one both reach the world THROUGH THE LATCH,
// which is what keeps a session replayable — `Isles().current` is world state
// and is in the digest, so a screen that moved it directly would work perfectly
// and silently stop the session reproducing. These cover that path, not the
// verb, which the cases above already own.

#include "hf/features/economy/system.hpp"
#include "hf/runtime/game_world.hpp"

namespace {

// One economy step with a given latch, over a small world.
runtime::GameWorld StepWith(const runtime::LatchedInput& input,
                            aether::U32 coin, economy::EconomySystem& economy) {
  runtime::GameWorld world(/*seed=*/1, /*plot_count=*/4);
  world.AddSystem(economy);
  world.World().ThePurse().coin = coin;
  world.Step(input, 1.0f / 60.0f);
  return world;
}

}  // namespace

TEST_CASE("BUYING AN ISLAND GOES THROUGH THE LATCH AND SPENDS THE PURSE") {
  economy::EconomySystem economy;
  const U32 price = content::kIslands[content::kNearIsle].unlock_coins;

  SUBCASE("with the coin, the island becomes the player's") {
    runtime::LatchedInput input;
    input.unlock_island = content::kNearIsle;
    const runtime::GameWorld world = StepWith(input, price + 5, economy);
    CHECK(runtime::IsUnlocked(world.World().Isles(), content::kNearIsle));
    CHECK(world.World().ThePurse().coin == 5);
  }

  SUBCASE("without it, nothing is bought and nothing is spent") {
    runtime::LatchedInput input;
    input.unlock_island = content::kNearIsle;
    const runtime::GameWorld world = StepWith(input, price - 1, economy);
    CHECK_FALSE(runtime::IsUnlocked(world.World().Isles(), content::kNearIsle));
    CHECK(world.World().ThePurse().coin == price - 1);
  }
}

TEST_CASE("TRAVEL MOVES THE WORLD, AND ONLY TO AN ISLAND YOU OWN") {
  economy::EconomySystem economy;

  SUBCASE("a locked island is refused and the player does not move") {
    runtime::LatchedInput input;
    input.travel_to = content::kFarIsle;
    const runtime::GameWorld world = StepWith(input, 0, economy);
    CHECK(world.World().Isles().current == content::kHub);
  }

  SUBCASE(
      "buying and travelling in ONE step works, and the order is the "
      "order the systems run in") {
    // Both verbs land in the same latch, and CrossTheSky unlocks before it
    // travels — so a player who taps Buy and then Go inside one frame is not
    // told the island is not theirs.
    runtime::LatchedInput input;
    input.unlock_island = content::kNearIsle;
    input.travel_to = content::kNearIsle;
    const runtime::GameWorld world = StepWith(
        input, content::kIslands[content::kNearIsle].unlock_coins, economy);
    CHECK(runtime::IsUnlocked(world.World().Isles(), content::kNearIsle));
    CHECK(world.World().Isles().current == content::kNearIsle);
  }

  SUBCASE(
      "the sim EMITS the departure — app/ animates it, and cannot be the "
      "one to decide it") {
    runtime::GameWorld world(/*seed=*/1, /*plot_count=*/4);
    world.AddSystem(economy);
    world.World().Isles().unlocked |= (1u << content::kNearIsle);
    runtime::LatchedInput input;
    input.travel_to = content::kNearIsle;
    const runtime::EventList& out = world.Step(input, 1.0f / 60.0f);
    bool departed = false;
    for (const runtime::GameEvent& event : out) {
      if (const auto* left = std::get_if<runtime::Departed>(&event)) {
        departed = true;
        CHECK(left->from == content::kHub);
        CHECK(left->to == content::kNearIsle);
      }
    }
    CHECK(departed);
  }
}
