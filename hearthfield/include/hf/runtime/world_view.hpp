// The shared, mutable sim state features read and write during the fixed step
// — the only channel besides events.
//
// A SIM header: it names no scene, no renderer and no device, which is what
// makes the headless claim true by construction rather than by intent.
#pragma once

#include <span>
#include <vector>

#include "aether/core/types.hpp"
#include "hf/runtime/archipelago.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/farm.hpp"
#include "hf/runtime/rng.hpp"
#include "hf/runtime/tick.hpp"

namespace hearthfield::runtime {

class WorldView {
 public:
  explicit WorldView(aether::U64 seed = 1, aether::Usize plot_count = 1)
      : plots_(plot_count), rng_(seed) {
    // A FRESH WORLD OWNS ITS WHOLE BOARD. Defaulting `unlocked` to 0 made a
    // default-constructed world one where no tap does anything — which broke
    // five tests silently the moment land ownership landed, and turned one of
    // them into an infinite loop rather than a failure. app/ narrows this for a
    // genuinely new game; a save always states it outright.
    land_.owned = static_cast<aether::U32>(plot_count);
  }

  // The farm clock. Moved by GameWorld ONLY, once per step, before any system
  // runs — so every system in a step sees the same `now`.
  [[nodiscard]] Tick Now() const { return now_; }
  void AdvanceBy(Tick ticks) { now_ += ticks; }

  [[nodiscard]] std::span<Plot> Plots() { return plots_; }
  [[nodiscard]] std::span<const Plot> Plots() const { return plots_; }
  [[nodiscard]] bool HasPlot(PlotId id) const { return id < plots_.size(); }

  [[nodiscard]] Rng& Random() { return rng_; }
  [[nodiscard]] const Rng& Random() const { return rng_; }

  // The chain (H3). Shared: production and economy both write the barn, orders
  // and economy both write the purse.
  [[nodiscard]] Barn& TheBarn() { return barn_; }
  [[nodiscard]] const Barn& TheBarn() const { return barn_; }
  [[nodiscard]] std::span<Building> Buildings() { return buildings_; }
  [[nodiscard]] std::span<const Building> Buildings() const {
    return buildings_;
  }
  [[nodiscard]] OrderBoard& Board() { return board_; }
  [[nodiscard]] const OrderBoard& Board() const { return board_; }
  // SHARED across islands, and PER ISLAND, split at the second farm's n1. The
  // accessor is `ThePurse` rather than `Purse` because the type is now called
  // that; it also matches TheBarn/TheCoop/TheLand, which is what the rest of
  // this surface already reads like.
  [[nodiscard]] Purse& ThePurse() { return purse_; }
  [[nodiscard]] const Purse& ThePurse() const { return purse_; }
  [[nodiscard]] Land& TheLand() { return land_; }
  [[nodiscard]] const Land& TheLand() const { return land_; }
  [[nodiscard]] Coop& TheCoop() { return coop_; }
  [[nodiscard]] const Coop& TheCoop() const { return coop_; }
  // The archipelago (s4): which islands are owned, and which one this farm's
  // player is standing on. In the world rather than beside it because it is
  // saved, digested and restored with everything else — the three lists below.
  [[nodiscard]] Archipelago& Isles() { return isles_; }
  [[nodiscard]] const Archipelago& Isles() const { return isles_; }

  void SetBuildingCount(aether::Usize count) { buildings_.resize(count); }

  // Append one, and say which it became. The placement system's commit, and the
  // only way a building is created outside a load — so the id it returns is
  // what an event carries back to the view.
  aether::U32 AddBuilding(content::BuildingKind kind, BuildCell cell) {
    buildings_.push_back(Building{.kind = kind, .cell = cell});
    return static_cast<aether::U32>(buildings_.size() - 1);
  }

  // Replace the whole world with a loaded one.
  //
  // THREE LISTS MUST AGREE: what this restores, what the save carries, and what
  // Digest() hashes. Adding a field to the world and forgetting the third makes
  // the save round-trip test PASS while the field is not persisted — the test
  // hides the omission instead of catching it. Digest is the one that notices,
  // so it is the one to update first.
  void Restore(Tick now, aether::U64 rng_state, std::span<const Plot> plots,
               const Barn& barn, std::span<const Building> buildings,
               const OrderBoard& board, const Purse& purse, const Land& land,
               const Coop& coop, const Archipelago& isles) {
    now_ = now;
    rng_.SetState(rng_state);
    plots_.assign(plots.begin(), plots.end());
    barn_ = barn;
    buildings_.assign(buildings.begin(), buildings.end());
    board_ = board;
    purse_ = purse;
    land_ = land;
    coop_ = coop;
    isles_ = isles;
  }

  // A stable fingerprint of everything that makes this farm the farm it is.
  //
  // It is what turns "the same world" into an assertion — the replay and
  // offline-equivalence tests both compare digests, and H2's save round-trip
  // and H6's headless oracle want the same value. Hashed FIELD BY FIELD and
  // BYTE BY BYTE rather than over the struct's memory, so it carries no
  // padding and no endianness and can therefore be compared across platforms.
  [[nodiscard]] aether::U64 Digest() const {
    aether::U64 hash = 0xCBF29CE484222325ULL;  // FNV-1a offset basis
    hash = Mix(hash, now_);
    hash = Mix(hash, rng_.State());
    hash = Mix(hash, static_cast<aether::U64>(plots_.size()));
    for (const Plot& plot : plots_) {
      hash = Mix(hash, static_cast<aether::U64>(plot.state));
      hash = Mix(hash, static_cast<aether::U64>(plot.crop));
      hash = Mix(hash, plot.sown_tick);
      hash = Mix(hash, plot.ready_tick);
    }
    // The chain. Added in the SAME change as the state itself (the plan's §3c):
    // a field in the world but not in the digest makes every test that asserts
    // on this value blind to it.
    for (const aether::U32 count : barn_.counts) {
      hash = Mix(hash, count);
    }
    hash = Mix(hash, barn_.capacity);
    hash = Mix(hash, static_cast<aether::U64>(buildings_.size()));
    for (const Building& building : buildings_) {
      hash = Mix(hash, building.queued);
      hash = Mix(hash, building.done_tick);
      for (const content::RecipeId recipe : building.queue) {
        hash = Mix(hash, recipe);
      }
      // v4's fields, in the digest for the reason stated above: a placement is
      // a sim state change, so a replay that placed a building somewhere else
      // has to differ HERE or the oracle cannot see it.
      hash = Mix(hash, building.kind);
      hash = Mix(hash, building.cell.col);
      hash = Mix(hash, building.cell.row);
    }
    for (const Order& order : board_.slots) {
      hash = Mix(hash, order.item);
      hash = Mix(hash, order.count);
      hash = Mix(hash, order.reward);
      hash = Mix(hash, static_cast<aether::U64>(order.active ? 1 : 0));
    }
    hash = Mix(hash, board_.next_refresh);
    hash = Mix(hash, purse_.coin);
    hash = Mix(hash, land_.owned);
    hash = Mix(hash, coop_.animals);
    hash = Mix(hash, coop_.fed_until);
    hash = Mix(hash, coop_.next_lay);
    // v5's archipelago, in the digest for the reason stated above: buying an
    // island spends coin, so a replay that bought one has to differ HERE as
    // well as in the purse, or the oracle cannot tell the two runs apart.
    hash = Mix(hash, isles_.unlocked);
    hash = Mix(hash, isles_.current);
    return hash;
  }

 private:
  static constexpr aether::U64 Mix(aether::U64 hash, aether::U64 value) {
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= (value >> (byte * 8)) & 0xFFULL;
      hash *= 0x100000001B3ULL;  // FNV-1a prime
    }
    return hash;
  }

  Tick now_ = 0;
  std::vector<Plot> plots_;
  Rng rng_;  // THE world RNG; every roll in the sim draws from it
  Barn barn_;
  std::vector<Building> buildings_;
  OrderBoard board_;
  Purse purse_;
  Land land_;
  Coop coop_;
  Archipelago isles_;
};

}  // namespace hearthfield::runtime
