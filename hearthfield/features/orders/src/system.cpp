#include "hf/features/orders/system.hpp"

#include <algorithm>

#include "hf/content/items.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::orders {
namespace {

using runtime::Order;
using runtime::Tick;

// Whether an item comes out of a building rather than the ground. Refined
// goods are asked for in SMALLER quantities, and that is a balance rule with a
// hard reason behind it: a starting farm is four plots, so an order for five
// flour (ten wheat, two full grinds) cannot be met at all — a board whose
// orders are unfillable for the first hour is not a board.
[[nodiscard]] bool IsRefined(content::ItemId item) {
  return item == content::kFlour || item == content::kMeal;
}

// What an order pays, per unit. Refined goods are worth more than raw ones,
// which is the entire reason to own a mill.
[[nodiscard]] aether::U32 UnitPrice(content::ItemId item) {
  switch (item) {
    case content::kFlour:
      return 14;
    case content::kMeal:
      return 20;
    case content::kCornItem:
      return 6;
    default:
      return 3;
  }
}

}  // namespace

void OrdersSystem::Step(runtime::StepContext& ctx,
                        const runtime::EventList& /*in*/,
                        runtime::EventList& out) {
  runtime::OrderBoard& board = ctx.world.Board();
  const Tick now = ctx.world.Now();
  const Tick interval = runtime::TicksFromSeconds(kRefreshSeconds);

  if (board.next_refresh == 0) {
    board.next_refresh = now + interval;  // a brand-new farm
    return;
  }
  if (now < board.next_refresh) {
    return;
  }

  // HOW MANY POSTINGS WERE MISSED, BY DIVISION — not by looping the deadline
  // forward one interval at a time.
  //
  // The loop version is the obvious one and it is wrong in two ways at once. It
  // is UNBOUNDED (a month away at half-hour postings is 1440 turns), and the
  // tempting bounded fix — "if the board is full, set next_refresh = now +
  // interval" — silently breaks the offline equivalence: stepping N ticks lands
  // the deadline on a multiple of the interval, jumping N ticks lands it on
  // `now + interval`, and those are different numbers. Integer division gives
  // the same answer whichever way the clock got here.
  const Tick missed = ((now - board.next_refresh) / interval) + 1;

  const auto free_slots = static_cast<Tick>(std::ranges::count_if(
      board.slots, [](const Order& o) { return !o.active; }));
  // Bounded by a GAME RULE rather than a clamp: the board only posts into a
  // free slot, so "the board was full when you got back" is both what a player
  // expects and what stops the work.
  const Tick postings = std::min(missed, free_slots);

  for (Tick made = 0; made < postings; ++made) {
    const auto slot = static_cast<aether::U8>(std::distance(
        board.slots.begin(),
        std::ranges::find_if(board.slots,
                             [](const Order& o) { return !o.active; })));
    // Every draw is from THE world RNG, in a fixed order, so a replay and a
    // reloaded save both produce the same board.
    const auto item = static_cast<content::ItemId>(
        ctx.world.Random().NextBelow(content::kItemCount));
    const aether::U32 count = IsRefined(item)
                                  ? 1 + ctx.world.Random().NextBelow(2)
                                  : 2 + ctx.world.Random().NextBelow(4);
    board.slots[slot] = Order{.item = item,
                              .count = count,
                              .reward = count * UnitPrice(item),
                              .active = true};
    out.emplace_back(runtime::OrderPosted{.slot = slot});
  }
  board.next_refresh += missed * interval;
}

}  // namespace hearthfield::orders
