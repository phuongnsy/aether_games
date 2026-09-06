// THE STAMPED DIGEST — spec check 9, and the reason the other five milestones
// can be trusted.
//
// A WORLD digest, not a pixel one, and that is the decision rather than a
// consolation. `WorldView::Digest()` is FNV-1a mixed field by field and byte by
// byte over integers, built that way at H0 precisely so it could be compared
// across machines — so this runs in `pixi run test`, on every platform, in CI,
// where a capture oracle runs nowhere (no GPU, and captures-check is calibrated
// to one toolchain). It also fails on exactly what this game IS: a
// deterministic simulation that survives an absence and a file.
//
// What it cannot see is the picture. A board rendered upside down would not
// move this number by one bit — that is the deferred pixel rule's job, and the
// two are complements.
#include "hf/runtime/script.hpp"

#include <doctest/doctest.h>

#include <ios>

#include "hf/content/animals.hpp"
#include "hf/features/economy/system.hpp"
#include "hf/features/livestock/system.hpp"
#include "hf/features/orders/system.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/features/production/system.hpp"
#include "hf/runtime/game_world.hpp"

using namespace aether;
using hearthfield::runtime::GameWorld;
using hearthfield::runtime::LatchedInput;
namespace content = hearthfield::content;
namespace runtime = hearthfield::runtime;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;
constexpr U32 kColumns = 8;

// The same farm app/'s Load builds for a NEW game, and the same registration
// order (spec §5.1). If these drift apart the digest below stops describing the
// game, so they are spelled next to each other rather than in two files.
struct Session {
  GameWorld world{/*seed=*/1, static_cast<Usize>(kColumns) * kColumns};
  hearthfield::plots::PlotsSystem plots;
  hearthfield::production::ProductionSystem production;
  hearthfield::livestock::LivestockSystem livestock;
  hearthfield::orders::OrdersSystem orders;
  hearthfield::economy::EconomySystem economy;

  Session() {
    runtime::WorldView& farm = world.World();
    farm.SetBuildingCount(1);
    farm.ThePurse().coin = hearthfield::economy::kStartingCoin;
    farm.TheLand().owned = hearthfield::economy::kStartingPlots;
    farm.TheCoop().animals = content::kFlockSize;
    world.AddSystem(plots);
    world.AddSystem(production);
    world.AddSystem(livestock);
    world.AddSystem(orders);
    world.AddSystem(economy);
  }

  [[nodiscard]] U8 FillableSlot() const {
    const runtime::Barn& barn = world.World().TheBarn();
    const runtime::OrderBoard& board = world.World().Board();
    for (Usize slot = 0; slot < runtime::kOrderSlots; ++slot) {
      const runtime::Order& order = board.slots[slot];
      if (order.active && barn.Of(order.item) >= order.count) {
        return static_cast<U8>(slot);
      }
    }
    return LatchedInput::kNoSlot;
  }

  // ONE BEAT PER STEP, which is what app/'s FixedUpdate does. Keyed off the
  // render frame it was not reproducible at all — a beat could be overwritten
  // before any fixed step consumed it.
  void Play() {
    for (U32 beat = 0; beat <= runtime::kScriptBeats; ++beat) {
      world.Step(
          runtime::ScriptBeat(beat,
                              runtime::ScriptContext{
                                  .plot_count = kColumns * kColumns,
                                  .fillable_slot = FillableSlot(),
                                  .order_refresh = runtime::TicksFromSeconds(
                                      hearthfield::orders::kRefreshSeconds)}),
          kDt);
    }
  }
};

}  // namespace

TEST_CASE("THE SCRIPTED SESSION HAS A STAMPED DIGEST") {
  // RE-STAMPED DELIBERATELY, NEVER "updated to make it pass". If this moves,
  // something about the simulation moved, and the question is whether that was
  // intended — a crop's timer, a recipe's yield, the order board's rolls, the
  // coop's arithmetic, the RNG, or the save's idea of what a farm is.
  //
  // RE-STAMPED 2026-08-29 for ONE cause, sky world s4: the world gained an
  // ARCHIPELAGO — which islands are owned and which one the player stands on —
  // and both fields are now in the digest. Deliberate for the same reason H7's
  // were: buying an island spends coin, so a replay that bought one has to
  // differ HERE as well as in the purse or the oracle cannot tell the runs
  // apart. THE SCRIPT ITSELF BUYS NOTHING, so what moved the value is the two
  // new fields entering the hash at their defaults (the hub, owned and stood
  // upon) and not any change to what the session does. Nothing about a crop, a
  // recipe, the board, the coop or the RNG moved; the previous value was
  // 0x59a889935bb0e347.
  //
  // RE-STAMPED 2026-08-24 for ONE cause, H7: a building gained a KIND and a
  // CELL, both of which are now in the world digest. That is deliberate rather
  // than incidental — a placement is a state change, so a replay that put a
  // mill somewhere else has to differ HERE or this oracle cannot see it. The
  // starter mill also stopped being at the default cell and now stands where
  // the authored locator does (kStarterMillCell). Nothing about a crop, a
  // recipe, the board or the RNG moved; the previous value was
  // 0x38e66d7c3095ee67.
  constexpr U64 kStamped = 0xc1dfb213b3f644c6ULL;

  Session session;
  session.Play();
  const U64 digest = session.world.World().Digest();
  CHECK_MESSAGE(digest == kStamped, "the scripted session produced 0x"
                                        << std::hex << digest
                                        << " — see the comment above");
}

TEST_CASE("the digest is a function of the SCRIPT, not of how it was played") {
  // Two sessions, same beats, same answer. Cheap, and it is what makes the
  // stamp above meaningful: a value that varied run to run would be a
  // coincidence recorded as an oracle.
  Session a;
  Session b;
  a.Play();
  b.Play();
  CHECK(a.world.World().Digest() == b.world.World().Digest());
}

TEST_CASE("EVERY BEAT DOES SOMETHING") {
  // A beat that silently produced no input would shorten the session without
  // shortening the script, and the digest would still look stamped. Beat 0 is
  // the idle beat before anything happens and is excluded by name.
  const runtime::ScriptContext ctx{
      .plot_count = kColumns * kColumns,
      .fillable_slot = 1,
      .order_refresh = runtime::TicksFromSeconds(1800.0)};
  const LatchedInput idle;
  for (U32 beat = 1; beat < runtime::kScriptBeats; ++beat) {
    const LatchedInput in = runtime::ScriptBeat(beat, ctx);
    const bool acts = in.tap || in.offline_ticks != 0 || in.feed_coop ||
                      in.buy_land || in.buy_barn ||
                      in.queue_recipe != LatchedInput::kNoRecipe ||
                      in.fill_slot != LatchedInput::kNoSlot;
    CHECK_MESSAGE(acts, "beat " << beat << " is a no-op");
  }
  CHECK(runtime::ScriptBeat(0, ctx).tap == idle.tap);
}

TEST_CASE("the script plays the WHOLE chain, not just the easy half") {
  // The stamp is one number and says nothing about what happened. These are the
  // claims the session is supposed to demonstrate, asserted rather than
  // believed — a script that stopped after sowing would still have a stable
  // digest.
  Session session;
  session.Play();
  const runtime::WorldView& farm = session.world.World();

  CHECK(farm.Now() > runtime::TicksFromSeconds(3600.0));  // hours passed
  CHECK(farm.TheCoop().fed_until > 0);            // the trough was filled
  CHECK(farm.TheBarn().Of(content::kEgg) > 0);    // and the birds laid
  CHECK(farm.TheBarn().Of(content::kFlour) > 0);  // the mill produced
  U32 posted = 0;
  for (const runtime::Order& order : farm.Board().slots) {
    posted += order.active ? 1 : 0;
  }
  CHECK(posted > 0);  // the board woke up
}
