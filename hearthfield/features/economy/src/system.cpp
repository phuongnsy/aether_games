#include "hf/features/economy/system.hpp"

#include <variant>

#include "hf/content/crops.hpp"
#include "hf/content/items.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::economy {

namespace {

// The six phases of an economy step, each independent: every one reads
// `ctx`/`in` and appends to `out`, and none reads what another wrote. That is
// what lets Step below read as a list rather than a sequence.

// Harvest -> barn. Reads another feature's events; writes deposits.
void DepositHarvests(const runtime::EventList& in, runtime::EventList& out,
                     runtime::Barn& barn) {
  // A harvest reaches the barn HERE rather than in the plots feature, and that
  // is the dependency law working: plots knows nothing about a barn, it only
  // says what came off the ground. Reading `in` is how a feature consumes
  // another's output without either including the other's headers.
  for (const runtime::GameEvent& event : in) {
    const auto* harvested = std::get_if<runtime::Harvested>(&event);
    if (harvested == nullptr || !content::CropExists(harvested->crop)) {
      continue;
    }
    const content::Crop& crop = content::CropById(harvested->crop);
    const aether::U32 stored = barn.Add(crop.yields, harvested->amount);
    out.emplace_back(runtime::Deposited{.item = crop.yields,
                                        .stored = stored,
                                        .lost = harvested->amount - stored});
    if (stored < harvested->amount) {
      out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoRoom});
    }
  }
}

// One order slot, filled from the barn for coin.
void ShipOrder(runtime::StepContext& ctx, runtime::EventList& out,
               runtime::Barn& barn, runtime::Purse& purse) {
  // Ship an order.
  if (ctx.input.fill_slot != runtime::LatchedInput::kNoSlot &&
      ctx.input.fill_slot < runtime::kOrderSlots) {
    runtime::Order& order = ctx.world.Board().slots[ctx.input.fill_slot];
    if (order.active && barn.Take(order.item, order.count)) {
      purse.coin += order.reward;
      out.emplace_back(runtime::OrderFilled{.slot = ctx.input.fill_slot,
                                            .reward = order.reward});
      order = runtime::Order{};
    } else {
      out.emplace_back(
          runtime::Refused{.why = runtime::Refused::Why::kNoItems});
    }
  }
}

// Coin -> items, refused whole rather than part-delivered.
void BuyGoods(runtime::StepContext& ctx, runtime::EventList& out,
              runtime::Barn& barn, runtime::Purse& purse) {
  // Buy goods.
  if (ctx.input.buy_item != runtime::LatchedInput::kNoItem &&
      ctx.input.buy_count > 0) {
    const aether::U32 price = PacketPrice(ctx.input.buy_count);
    if (!content::ItemExists(ctx.input.buy_item)) {
      out.emplace_back(
          runtime::Refused{.why = runtime::Refused::Why::kNoItems});
    } else if (purse.coin < price) {
      out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoCoin});
    } else if (barn.Space() < ctx.input.buy_count) {
      // Refused rather than part-delivered: paying full price for half a packet
      // because the barn was nearly full is the kind of thing a player
      // remembers. A harvest is different — nobody chose for it to arrive.
      out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoRoom});
    } else {
      purse.coin -= price;
      const aether::U32 stored =
          barn.Add(ctx.input.buy_item, ctx.input.buy_count);
      out.emplace_back(runtime::Deposited{
          .item = ctx.input.buy_item, .stored = stored, .lost = 0});
    }
  }
}

// Coin -> barn capacity.
void ExpandBarn(runtime::StepContext& ctx, runtime::EventList& out,
                runtime::Barn& barn, runtime::Purse& purse) {
  // Expand the barn.
  if (ctx.input.buy_barn) {
    const aether::U32 price = BarnPrice(barn.capacity);
    if (purse.coin < price) {
      out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoCoin});
    } else {
      purse.coin -= price;
      barn.capacity += kBarnStep;
    }
  }
}

// Coin -> one more owned plot. THE ONE PHASE THAT SPENDS SHARED MONEY ON
// PER-ISLAND LAND, so it takes both halves of the old Economy explicitly — and
// that pairing is exactly why the split had to happen before a second farm.
void ExpandFarm(runtime::StepContext& ctx, runtime::EventList& out,
                runtime::Purse& purse, runtime::Land& land) {
  // Expand the farm.
  if (ctx.input.buy_land) {
    const aether::U32 price = LandPrice(land.owned);
    const auto total = static_cast<aether::U32>(ctx.world.Plots().size());
    if (land.owned >= total) {
      out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoRoom});
    } else if (purse.coin < price) {
      out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoCoin});
    } else {
      purse.coin -= price;
      out.emplace_back(runtime::LandBought{
          .plot = static_cast<runtime::PlotId>(land.owned), .cost = price});
      ++land.owned;
    }
  }
}

// Buy an island, and go to one. HERE rather than in a system of their own
// because both are purchases against the same purse — an island is the largest
// thing on the shop's shelf, not a separate economy. Travel costs nothing and
// rides along because it is the other half of the same screen.
void CrossTheSky(runtime::StepContext& ctx, runtime::EventList& out,
                 runtime::Purse& purse) {
  runtime::Archipelago& isles = ctx.world.Isles();

  if (ctx.input.unlock_island != runtime::LatchedInput::kNoIsland) {
    const content::IslandId id = ctx.input.unlock_island;
    switch (runtime::UnlockIsland(isles, purse, id)) {
      case runtime::UnlockResult::kBought:
        out.emplace_back(runtime::IslandBought{
            .island = id, .cost = content::kIslands[id].unlock_coins});
        break;
      case runtime::UnlockResult::kCannotAfford:
        out.emplace_back(
            runtime::Refused{.why = runtime::Refused::Why::kNoCoin});
        break;
      // Already owned, or no such island: the screen offers neither, so both
      // mean a stale click rather than a refusal worth telling the player
      // about.
      case runtime::UnlockResult::kAlreadyOwned:
      case runtime::UnlockResult::kNoSuchIsland:
        break;
    }
  }

  // THE SIM DECIDES WHERE YOU ARE, and it decides instantly. app/ turns this
  // event into a two-second crossing, which is presentation over a fact — the
  // same division a crop's growth already makes. Routing it through the latch
  // rather than letting app/ set `current` is what keeps a session replayable.
  const content::IslandId to = ctx.input.travel_to;
  if (to != runtime::LatchedInput::kNoIsland && to != isles.current) {
    if (runtime::IsUnlocked(isles, to)) {
      out.emplace_back(runtime::Departed{.from = isles.current, .to = to});
      isles.current = to;
    } else {
      out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoCoin});
    }
  }
}

}  // namespace

void EconomySystem::Step(runtime::StepContext& ctx,
                         const runtime::EventList& in,
                         runtime::EventList& out) {
  runtime::Barn& barn = ctx.world.TheBarn();
  runtime::Purse& purse = ctx.world.ThePurse();

  DepositHarvests(in, out, barn);
  ShipOrder(ctx, out, barn, purse);
  BuyGoods(ctx, out, barn, purse);
  ExpandBarn(ctx, out, barn, purse);
  ExpandFarm(ctx, out, purse, ctx.world.TheLand());
  CrossTheSky(ctx, out, purse);
}

}  // namespace hearthfield::economy
