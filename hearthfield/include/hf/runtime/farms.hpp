// Every island's farm — one LIVE, the rest dormant.
//
// The second farm's headline decision (n0, ratified 2026-08-29): only the
// island the player stands on is simulated. The others are `SavedFarm` records
// with a `saved_at`, and arriving catches one up over the elapsed time using
// ADR-0106's closed-form maths — the SAME mechanism a farm already uses to
// survive the game being shut, applied across travel instead of across a
// restart.
//
// WHAT THAT BUYS is design goal 2: not one of the six feature systems learns
// this exists. They step a `WorldView`, and there is still exactly one.
//
// WHAT IT COSTS was accepted rather than discovered: you cannot see a dormant
// farm's state without travelling to it. No badge, no notification.
//
// THIS CLASS DOES NOT STEP ANYTHING, and that is deliberate. Catch-up is not a
// function here — it is `LatchedInput::offline_ticks`, which `GameWorld::Step`
// already consumes and which the replay stream already records. `Enter` returns
// the ticks owed and the caller latches them, so an arrival replays exactly
// like a relaunch and needs no second code path to be right.
#pragma once

#include <span>
#include <vector>

#include "aether/core/types.hpp"
#include "hf/content/islands.hpp"
#include "hf/runtime/save.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::runtime {

// What `Enter` found, so the caller can tell "your farm, grown" from "bare
// ground you have never worked". app/ owns what a first visit STARTS with —
// this only reports which happened, because a starting hand is content policy
// and does not belong in a container.
enum class Arrival : aether::U8 {
  kReturned,   // a record existed; `offline_ticks` says how long you were away
  kFirstVisit  // nothing was here; the live farm was reset to an empty board
};

struct Landing {
  Arrival what = Arrival::kFirstVisit;
  // Ticks owed to the island just entered, for the caller to latch. Zero on a
  // first visit — there is no absence to catch up on.
  aether::U32 offline_ticks = 0;
};

class Farms {
 public:
  // Write the live farm into `from`'s record, stamped with `now`. Called on
  // DEPARTURE; the stamp is the other end of the next arrival's catch-up, so
  // forgetting it would make a returning farm think no time had passed.
  void Park(content::IslandId from, const WorldView& live, aether::I64 now,
            aether::U32 columns) {
    Grow(from);
    records_[from] = CaptureFarm(live, now, columns);
    present_[from] = 1;
  }

  // Install `to`'s farm into `live` and say what was found.
  //
  // THE SHARED HALF IS PRESERVED, which is the whole reason this is not just
  // RestoreFarm: the purse and the archipelago belong to the PLAYER and follow
  // them across the water, while plots, barn, buildings, board, coop and owned
  // land belong to the island and stay behind. That is the line n1 drew in the
  // types, enforced here in the one place it could be crossed.
  [[nodiscard]] Landing Enter(content::IslandId to, WorldView& live,
                              aether::I64 now, aether::Usize plot_count) {
    Grow(to);
    const Purse purse = live.ThePurse();
    const Archipelago isles = live.Isles();

    if (present_[to] == 0) {
      // Never worked. A blank board of the right size; what a new farm STARTS
      // with (coin, a mill, some owned plots) is app/'s call, not this one's.
      live = WorldView(/*seed=*/1, plot_count);
      live.ThePurse() = purse;
      live.Isles() = isles;
      return Landing{.what = Arrival::kFirstVisit, .offline_ticks = 0};
    }

    const SavedFarm& record = records_[to];
    live.Restore(record.tick, record.rng_state, record.plots, record.barn,
                 record.buildings, record.board, purse, record.land,
                 record.coop, isles);
    return Landing{.what = Arrival::kReturned,
                   .offline_ticks = OfflineTicksBetween(record.saved_at, now)};
  }

  // THE WHOLE PLAYER'S WORLD, not just the bit being looked at.
  //
  // `WorldView::Digest()` covers the LIVE farm and nothing else, so with the
  // second farm it stopped describing everything the player owns: a bug that
  // scrambled island 1's plots while you stood on island 0 would have moved no
  // oracle in this repo. Every test that compares worlds — the save round trip,
  // the replay, the offline equivalence — is only as wide as what it hashes.
  //
  // The ID IS MIXED IN as well as the record, so two islands swapping farms
  // does not cancel out. The order is the table's, which is the island id
  // order, so this is stable without sorting anything.
  [[nodiscard]] aether::U64 Digest(const WorldView& live) const {
    aether::U64 hash = live.Digest();
    for (aether::Usize id = 0; id < records_.size(); ++id) {
      if (present_[id] == 0) {
        continue;  // never worked; it has no state to describe
      }
      hash = Mix(hash, static_cast<aether::U64>(id));
      hash = Mix(hash, DigestOfRecord(records_[id]));
    }
    return hash;
  }

  [[nodiscard]] bool Worked(content::IslandId id) const {
    return id < present_.size() && present_[id] != 0;
  }
  // How many plots that island's board had when it was last parked. app/ needs
  // it to size the live world before entering — a farm's board is the SAVE's,
  // never the command line's (the rule LoadFarm already follows).
  [[nodiscard]] aether::U32 Columns(content::IslandId id) const {
    return Worked(id) ? records_[id].columns : 0;
  }

  // The records, for the save to write and read back. Const on the way out;
  // `Restore` is how they come in, because a save is the only other author.
  [[nodiscard]] std::span<const SavedFarm> Records() const { return records_; }
  [[nodiscard]] std::span<const aether::U8> Present() const { return present_; }
  void Restore(std::vector<SavedFarm> records,
               std::vector<aether::U8> present) {
    records_ = std::move(records);
    present_ = std::move(present);
  }

 private:
  // The same FNV-1a step WorldView::Digest uses, byte by byte over the value so
  // it carries no padding and no endianness — a digest that meant something
  // different on two machines would be worse than none.
  static constexpr aether::U64 Mix(aether::U64 hash, aether::U64 value) {
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= (value >> (byte * 8)) & 0xFFULL;
      hash *= 0x100000001B3ULL;
    }
    return hash;
  }

  // Islands are append-only ids and therefore indices; a table sized to the
  // highest one touched costs nothing and removes every bounds question below.
  void Grow(content::IslandId id) {
    if (id >= records_.size()) {
      records_.resize(static_cast<aether::Usize>(id) + 1);
      present_.resize(static_cast<aether::Usize>(id) + 1, 0);
    }
  }

  std::vector<SavedFarm> records_;
  // Parallel to `records_` rather than a flag inside SavedFarm: "never visited"
  // is not a state a SAVED farm can be in, and putting it in the record would
  // invite writing one to disk that says it does not exist.
  // U8 AND NOT bool: std::vector<bool> is the bitset specialisation, has no
  // contiguous storage, and cannot be spanned. A container that quietly is not
  // a container of what it says is not worth the byte it saves.
  std::vector<aether::U8> present_;
};

}  // namespace hearthfield::runtime
