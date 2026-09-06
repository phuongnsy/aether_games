// The chain's shared component data: what you own, what is being made, and who
// wants it.
//
// In `runtime` beside the plot table and for the same reason — production,
// orders and economy all read and write these, so no one feature may own them
// (the spec's §5 graph points features AT runtime).
#pragma once

#include <algorithm>
#include <array>

#include "aether/core/types.hpp"
#include "hf/content/buildings.hpp"
#include "hf/content/items.hpp"
#include "hf/content/recipes.hpp"
#include "hf/runtime/tick.hpp"

namespace hearthfield::runtime {

// ---- the barn --------------------------------------------------------------

// A fixed array rather than a map: the item catalogue is a compile-time table,
// so "how much flour" is an index. It also makes the save a fixed-size run of
// counts with no key encoding to get wrong.
struct Barn {
  std::array<aether::U32, content::kItemCount> counts{};
  aether::U32 capacity = 50;

  [[nodiscard]] aether::U32 Total() const {
    aether::U32 total = 0;
    for (const aether::U32 count : counts) {
      total += count;
    }
    return total;
  }
  [[nodiscard]] aether::U32 Space() const {
    const aether::U32 used = Total();
    return used >= capacity ? 0 : capacity - used;
  }
  [[nodiscard]] aether::U32 Of(content::ItemId item) const {
    return content::ItemExists(item) ? counts[item] : 0;
  }

  // Deposits what FITS and reports it. Deliberately partial rather than
  // all-or-nothing: a harvest into an almost-full barn should give the player
  // what there is room for, and the caller needs to know how much was lost to
  // say so.
  aether::U32 Add(content::ItemId item, aether::U32 count) {
    if (!content::ItemExists(item)) {
      return 0;
    }
    const aether::U32 fits = std::min(count, Space());
    counts[item] += fits;
    return fits;
  }

  // All-or-nothing, because a recipe half-paid-for is not a state anything
  // downstream can make sense of.
  [[nodiscard]] bool Take(content::ItemId item, aether::U32 count) {
    if (!content::ItemExists(item) || counts[item] < count) {
      return false;
    }
    counts[item] -= count;
    return true;
  }
};

// ---- buildings -------------------------------------------------------------

using BuildingId = aether::U32;

// A cell on the BUILD RING (build_ring.hpp holds the lattice and the rules).
// It lives here, with the building that stands on it, because build_ring.hpp
// reads this table and the dependency may only run one way.
//
// Unsigned indices from the ring's corner, not world offsets: a signed
// world-relative pair would need the ring's size to be interpreted at all, and
// this has to survive a save.
struct BuildCell {
  aether::U16 col = 0;
  aether::U16 row = 0;

  [[nodiscard]] constexpr bool operator==(const BuildCell&) const = default;
};

// Four is a design number, not a limit of the container: a queue long enough to
// cover a night's absence, short enough that the player still has to come back.
inline constexpr aether::Usize kQueueCapacity = 4;

// THE INPUTS ARE ALREADY PAID FOR. Queuing debits the barn, so everything in
// here is committed and the building's future depends on nothing but its own
// queue and the clock — which is what keeps an absence closed-form (spec risk
// 5, and the plan's §3a).
struct Building {
  std::array<content::RecipeId, kQueueCapacity> queue{};
  aether::U8 queued = 0;
  // When the head finishes. Meaningless while `queued` is 0.
  Tick done_tick = 0;
  // WHAT and WHERE, added at H7. Until then a building was a queue and a timer
  // with no position at all, and the mill the player saw was a mesh authored in
  // farm.world.json that nothing connected to this table.
  //
  // `cell` is on the BUILD RING (build_ring.hpp), not the plot grid — the two
  // lattices coincide, and the ring's central square IS the field.
  content::BuildingKind kind = content::kMillKind;
  BuildCell cell{};

  [[nodiscard]] bool Busy() const { return queued > 0; }
  [[nodiscard]] bool Full() const { return queued >= kQueueCapacity; }
};

// ---- the order board -------------------------------------------------------

struct Order {
  content::ItemId item = 0;
  aether::U32 count = 0;
  aether::U32 reward = 0;
  bool active = false;
};

// Three slots, and the count is load-bearing rather than cosmetic: the board
// only generates into a FREE slot, so the catch-up after a month away runs at
// most three times instead of once per refresh interval (the plan's §3b).
inline constexpr aether::Usize kOrderSlots = 3;

struct OrderBoard {
  std::array<Order, kOrderSlots> slots{};
  Tick next_refresh = 0;

  [[nodiscard]] bool Full() const {
    return std::ranges::all_of(slots, [](const Order& o) { return o.active; });
  }
};

// ---- the coop --------------------------------------------------------------

// THE TROUGH IS THE MILL'S QUEUE, ONE FEATURE OVER. Filling it debits the barn
// and converts wheat into `fed_until`, an absolute tick — so from that moment
// the coop's future depends on two numbers it owns and the clock, and on
// nothing shared. That is ADR-0108's rule applied to the case the spec names by
// name for risk 5, and it is why an eight-hour absence is still arithmetic.
//
// A hen that took a grain each time it laid would be the alternative, and with
// two consumers against one barn the outcome would depend on the interleaving —
// which has no closed form and would have to be simulated tick by tick.
struct Coop {
  aether::U8 animals = 0;
  // Absolute, like every other deadline here. `now > fed_until` is a hungry
  // coop; it lays nothing and waits.
  Tick fed_until = 0;
  // When the next egg arrives. RESET BY A FILL rather than carried, so a coop
  // that starved for a week does not bank a week of eggs the moment it is fed
  // (H5 plan §3b).
  Tick next_lay = 0;

  [[nodiscard]] bool Fed(Tick now) const {
    return animals > 0 && now <= fed_until;
  }
};

// ---- the purse, and the land it buys ---------------------------------------
//
// ONE STRUCT UNTIL 2026-08-29, split because the two halves have different
// LIFETIMES once there is more than one island (the second-farm plan's n1).
// Coin is the player's and follows them everywhere; owned land is the island's
// and stays behind when they leave. Keeping them together would have made the
// per-island record carry the wallet, so travelling would either duplicate the
// player's money or leave it on the wrong rock.

// SHARED by every island. It is what buys islands, so splitting it per island
// would make which farm you were standing in decide whether you could afford
// the next one — a rule that reads as a bug.
struct Purse {
  aether::U32 coin = 0;
};

// PER ISLAND. Plots [0, owned) are the player's on THIS island.
//
// `owned`, not `unlocked`, and the rename is the point: `Archipelago::unlocked`
// is a bitmask of ISLANDS, and two fields a dot apart both called `unlocked`
// meaning different things is a trap that was already set (sky world s4) and is
// closed here.
//
// A v1 save migrates with this set to its whole board, because v1 had no locked
// land and taking it away on upgrade would be the worst possible first
// save-system impression.
struct Land {
  aether::U32 owned = 0;
};

}  // namespace hearthfield::runtime
