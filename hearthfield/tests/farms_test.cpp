// Dormant farms, and the one assertion the whole design rests on.
//
// The second farm's n0 ratified that only the island you stand on is simulated
// and the rest are caught up on arrival with ADR-0106's closed-form maths. That
// is a CLAIM: a farm left dormant for N seconds must end up in exactly the
// state a farm that was live for those N seconds is in. If it does not, the
// design is wrong rather than merely buggy — the whole point was reusing a
// mechanism already proven across a restart, and this is where "already proven"
// is checked rather than assumed (the plan's §5, and its fourth risk).
#include "hf/runtime/farms.hpp"

#include <doctest/doctest.h>

#include "hf/content/animals.hpp"
#include "hf/features/economy/system.hpp"
#include "hf/features/livestock/system.hpp"
#include "hf/features/orders/system.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/features/production/system.hpp"
#include "hf/runtime/game_world.hpp"

using namespace aether;
using namespace hearthfield;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;
constexpr Usize kPlots = 16;
// Long enough that crops finish and the coop lays — a gap too short to change
// anything would make the equality below true for the wrong reason.
constexpr I64 kAwaySeconds = 4000;

// A world with every system registered, in the spec's order (§5.1).
struct Sim {
  runtime::GameWorld world{/*seed=*/1, kPlots};
  plots::PlotsSystem plots;
  production::ProductionSystem production;
  livestock::LivestockSystem livestock;
  orders::OrdersSystem orders;
  economy::EconomySystem economy;

  Sim() {
    world.AddSystem(plots);
    world.AddSystem(production);
    world.AddSystem(livestock);
    world.AddSystem(orders);
    world.AddSystem(economy);
    runtime::WorldView& farm = world.World();
    farm.TheLand().owned = static_cast<U32>(kPlots);
    farm.TheCoop().animals = content::kFlockSize;
  }

  // Sow every plot, so there is something in flight to be caught up ON.
  void Sow() {
    for (Usize i = 0; i < kPlots; ++i) {
      runtime::LatchedInput in;
      in.tap = true;
      in.hovered = static_cast<runtime::PlotId>(i);
      in.sow_crop = content::kWheat;
      world.Step(in, kDt);
    }
  }

  void Idle(U32 steps) {
    for (U32 i = 0; i < steps; ++i) {
      world.Step(runtime::LatchedInput{}, kDt);
    }
  }
};

}  // namespace

TEST_CASE("A DORMANT FARM CAUGHT UP EQUALS ONE THAT WAS LIVE THROUGHOUT") {
  // THE ASSERTION THE DESIGN RESTS ON. Two identical farms; one is parked,
  // slept through `kAwaySeconds` of wall clock and re-entered, the other is
  // handed the same elapsed time as offline ticks the way a relaunch does. The
  // DIGESTS must match — not the plot table, the whole world, because the
  // digest is what every other oracle here compares.
  constexpr I64 kParkedAt = 1'000'000;

  Sim travelled;
  travelled.Sow();
  travelled.Idle(30);

  Sim stayed;
  stayed.Sow();
  stayed.Idle(30);
  REQUIRE(travelled.world.World().Digest() == stayed.world.World().Digest());

  // One travels: parked, away, and back.
  runtime::Farms farms;
  farms.Park(content::kHub, travelled.world.World(), kParkedAt,
             /*columns=*/4);
  // Somewhere else entirely in the meantime — the live world is scribbled over,
  // which is exactly what entering another island does to it.
  travelled.world.World() = runtime::WorldView(/*seed=*/99, kPlots);
  const runtime::Landing landing = farms.Enter(
      content::kHub, travelled.world.World(), kParkedAt + kAwaySeconds, kPlots);
  CHECK(landing.what == runtime::Arrival::kReturned);
  CHECK(landing.offline_ticks ==
        runtime::OfflineTicksBetween(kParkedAt, kParkedAt + kAwaySeconds));

  // The catch-up is LATCHED, not applied by Farms — the same path a relaunch
  // takes, which is the reason this equality is expected to hold at all.
  runtime::LatchedInput arrive;
  arrive.offline_ticks = landing.offline_ticks;
  travelled.world.Step(arrive, kDt);

  // The one that never left is handed the elapsed time computed INDEPENDENTLY,
  // not `landing.offline_ticks`. Reusing the landing's own number would make
  // the digest check circular — a Farms that reported the wrong absence would
  // hand the same wrong absence to both sides and they would still agree.
  // Mutation-proved: returning 0 ticks fails the digest comparison below.
  runtime::LatchedInput waited;
  waited.offline_ticks =
      runtime::OfflineTicksBetween(kParkedAt, kParkedAt + kAwaySeconds);
  stayed.world.Step(waited, kDt);

  CHECK(travelled.world.World().Digest() == stayed.world.World().Digest());
}

TEST_CASE("PARKING AND ENTERING KEEP THE PLAYER'S HALF AND SWAP THE ISLAND'S") {
  // n1 split Purse (shared, follows the player) from Land (per island, stays
  // behind). Farms::Enter is the ONE place that line could be crossed, so it is
  // the one place it is asserted.
  Sim sim;
  runtime::WorldView& farm = sim.world.World();
  farm.ThePurse().coin = 700;
  farm.TheLand().owned = 9;
  farm.TheBarn().Add(content::kWheatItem, 5);

  runtime::Farms farms;
  farms.Park(content::kHub, farm, /*now=*/500, /*columns=*/4);

  // Off to somewhere never worked, having earned more coin on the way.
  farm.ThePurse().coin = 900;
  const runtime::Landing fresh =
      farms.Enter(content::kNearIsle, farm, /*now=*/500, kPlots);
  CHECK(fresh.what == runtime::Arrival::kFirstVisit);
  CHECK(fresh.offline_ticks == 0);
  // The purse CROSSED with the player...
  CHECK(farm.ThePurse().coin == 900);
  // ...and the island's own state did not follow it.
  CHECK(farm.TheBarn().Of(content::kWheatItem) == 0);

  // Home again: the hub's barn and land are exactly as they were left, and the
  // coin earned elsewhere is still in hand.
  const runtime::Landing home =
      farms.Enter(content::kHub, farm, /*now=*/500, kPlots);
  CHECK(home.what == runtime::Arrival::kReturned);
  CHECK(farm.TheBarn().Of(content::kWheatItem) == 5);
  CHECK(farm.TheLand().owned == 9);
  CHECK(farm.ThePurse().coin == 900);
}

TEST_CASE("AN ISLAND NEVER WORKED REPORTS ITSELF AS SUCH") {
  runtime::Farms farms;
  CHECK_FALSE(farms.Worked(content::kHub));
  CHECK_FALSE(farms.Worked(content::kFarIsle));
  CHECK(farms.Columns(content::kFarIsle) == 0);

  Sim sim;
  farms.Park(content::kFarIsle, sim.world.World(), /*now=*/1, /*columns=*/4);
  CHECK(farms.Worked(content::kFarIsle));
  CHECK(farms.Columns(content::kFarIsle) == 4);
  // Parking one island says nothing about another — the table is per id, and a
  // shared flag would make the first visit anywhere look like a return.
  CHECK_FALSE(farms.Worked(content::kHub));
}

// --- the digest covers what the player OWNS, not what they are looking at ----

TEST_CASE("MUTATING A DORMANT FARM MOVES THE DIGEST") {
  // THE TEST THAT CATCHES n4 BEING FORGOTTEN. WorldView::Digest() covers the
  // live farm alone, so before this the whole of island 1 could be scrambled
  // while the player stood on island 0 and no oracle in this repo would have
  // moved — not the save round trip, not the replay, not the offline
  // equivalence. Every one of them is only as wide as what it hashes.
  Sim sim;
  runtime::Farms farms;
  farms.Park(content::kNearIsle, sim.world.World(), /*now=*/100, /*columns=*/4);

  const U64 before = farms.Digest(sim.world.World());

  // Reach into the dormant record the only way the save can: through Restore.
  std::vector<runtime::SavedFarm> records(farms.Records().begin(),
                                          farms.Records().end());
  std::vector<U8> present(farms.Present().begin(), farms.Present().end());
  records[content::kNearIsle].barn.Add(content::kWheatItem, 1);
  farms.Restore(std::move(records), std::move(present));

  CHECK(farms.Digest(sim.world.World()) != before);
}

TEST_CASE("THE DIGEST STILL MOVES WHEN THE LIVE FARM DOES") {
  // The other half, and not a formality: a combined digest that dropped the
  // live world would be just as blind, in the direction every existing oracle
  // depends on.
  Sim sim;
  runtime::Farms farms;
  farms.Park(content::kNearIsle, sim.world.World(), /*now=*/100, /*columns=*/4);

  const U64 before = farms.Digest(sim.world.World());
  sim.world.World().ThePurse().coin += 1;
  CHECK(farms.Digest(sim.world.World()) != before);
}

TEST_CASE("AN ISLAND NEVER WORKED CONTRIBUTES NOTHING TO THE DIGEST") {
  // A blank record must not be indistinguishable from a farm — but it also
  // must not make the digest depend on how big the table happens to have grown,
  // or parking island 2 would change the value for islands 0 and 1.
  Sim sim;
  runtime::Farms narrow;
  runtime::Farms wide;
  narrow.Park(content::kHub, sim.world.World(), /*now=*/100, /*columns=*/4);
  wide.Park(content::kHub, sim.world.World(), /*now=*/100, /*columns=*/4);
  // `wide` has a table stretched to cover the far isle, but nothing is in it.
  CHECK_FALSE(wide.Worked(content::kFarIsle));
  CHECK(narrow.Digest(sim.world.World()) == wide.Digest(sim.world.World()));
}

TEST_CASE("TWO ISLANDS SWAPPING FARMS DOES NOT CANCEL OUT") {
  // The id is mixed in as well as the record, so a bug that put the hub's farm
  // on the near isle and vice versa changes the digest rather than leaving it
  // identical — which a sum over records alone would not have caught.
  Sim a;
  a.world.World().ThePurse().coin = 1;
  Sim b;
  b.world.World().TheBarn().Add(content::kWheatItem, 3);

  runtime::Farms straight;
  straight.Park(content::kHub, a.world.World(), /*now=*/1, /*columns=*/4);
  straight.Park(content::kNearIsle, b.world.World(), /*now=*/1, /*columns=*/4);

  runtime::Farms swapped;
  swapped.Park(content::kHub, b.world.World(), /*now=*/1, /*columns=*/4);
  swapped.Park(content::kNearIsle, a.world.World(), /*now=*/1, /*columns=*/4);

  Sim live;
  CHECK(straight.Digest(live.world.World()) !=
        swapped.Digest(live.world.World()));
}

// --- the whole loop: two farms, through a file and back ----------------------

TEST_CASE("TWO FARMS SURVIVE A SAVE AND A RELAUNCH") {
  // n3 proved the format carries dormant records and n5 proved travel parks and
  // enters them. NEITHER PROVES THEY ARE WIRED TOGETHER, and the wiring is
  // app/'s: it decides which island the v1..v5 block describes and which go in
  // the appended list. This is that seam, headless — the plan's §7 acceptance
  // ("fly there, plant, fly home, come back later and find it grown") reduced
  // to the part a test can hold.
  constexpr I64 kLeft = 2'000'000;
  constexpr I64 kBack = kLeft + 6000;

  // On the hub, with a barn worth something and coin in hand.
  Sim hub;
  hub.world.World().ThePurse().coin = 250;
  hub.world.World().TheBarn().Add(content::kWheatItem, 11);
  hub.world.World().TheLand().owned = 12;
  hub.Sow();

  runtime::Farms farms;
  // Leave for the near isle: park the hub, arrive somewhere never worked.
  farms.Park(content::kHub, hub.world.World(), kLeft, /*columns=*/4);
  const runtime::Landing there =
      farms.Enter(content::kNearIsle, hub.world.World(), kLeft, kPlots);
  REQUIRE(there.what == runtime::Arrival::kFirstVisit);
  // UNLOCKED FIRST, and the test asserted otherwise until it did not: a
  // `current` naming an island the player does not own is sent home by
  // IslandsFromSave, exactly as designed. A player can only stand somewhere
  // they bought.
  hub.world.World().Isles().unlocked |= (1u << content::kNearIsle);
  hub.world.World().Isles().current = content::kNearIsle;
  hub.world.World().TheBarn().Add(content::kCornItem, 2);  // work it a little

  // SAVE, exactly as app/ does: the live island in the main block, every other
  // WORKED island appended.
  runtime::SavedFarm out =
      runtime::CaptureFarm(hub.world.World(), kBack, /*columns=*/4);
  for (content::IslandId id = 0; id < content::kIslands.size(); ++id) {
    if (id == hub.world.World().Isles().current || !farms.Worked(id)) {
      continue;
    }
    out.dormant_ids.push_back(id);
    out.dormant.push_back(farms.Records()[id]);
  }
  const auto reloaded = runtime::DecodeSave(runtime::EncodeSave(out));
  REQUIRE(reloaded.has_value());

  // RELAUNCH: a fresh world and a fresh container, filled only from the file.
  Sim fresh;
  runtime::RestoreFarm(fresh.world.World(), *reloaded);
  runtime::Farms after;
  {
    std::vector<runtime::SavedFarm> records(content::kIslands.size());
    std::vector<U8> present(content::kIslands.size(), 0);
    for (Usize i = 0; i < reloaded->dormant.size(); ++i) {
      records[reloaded->dormant_ids[i]] = reloaded->dormant[i];
      present[reloaded->dormant_ids[i]] = 1;
    }
    after.Restore(std::move(records), std::move(present));
  }

  // Standing where we left off, on the satellite, with what it grew.
  CHECK(fresh.world.World().Isles().current == content::kNearIsle);
  CHECK(fresh.world.World().TheBarn().Of(content::kCornItem) == 2);
  CHECK(fresh.world.World().TheBarn().Of(content::kWheatItem) == 0);
  CHECK(fresh.world.World().ThePurse().coin == 250);  // the purse crossed

  // ...and the HUB is still there, in the file, exactly as it was left.
  REQUIRE(after.Worked(content::kHub));
  const runtime::Landing home =
      after.Enter(content::kHub, fresh.world.World(), kBack, kPlots);
  CHECK(home.what == runtime::Arrival::kReturned);
  CHECK(home.offline_ticks == runtime::OfflineTicksBetween(kLeft, kBack));
  CHECK(fresh.world.World().TheBarn().Of(content::kWheatItem) == 11);
  CHECK(fresh.world.World().TheLand().owned == 12);
  // The coin is still the coin, having been nowhere near the file's dormant
  // half.
  CHECK(fresh.world.World().ThePurse().coin == 250);
}
