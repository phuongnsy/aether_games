// The farm on disk — the first thing this repo has ever written that has to
// outlive the process AND survive the next build.
//
// In `runtime` because it is pure data over the sim's own state: no device, no
// filesystem, fully testable headless. WHERE it is written is app/'s problem
// (platform::UserDataDir), and WHEN is app/'s too.
//
// Little-endian throughout, over core::ByteStream, inside a core::VersionedBlob
// (ADR-0103). Byte-by-byte rather than memcpy so a save does not depend on the
// machine that produced it — the first artefact here that could legitimately
// move between machines.
#pragma once

#include <span>
#include <vector>

#include "aether/core/byte_stream.hpp"
#include "aether/core/error.hpp"
#include "aether/core/types.hpp"
#include "aether/core/versioned_blob.hpp"
#include "hf/content/crops.hpp"
#include "hf/content/islands.hpp"
#include "hf/content/items.hpp"
#include "hf/runtime/build_ring.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/farm.hpp"
#include "hf/runtime/tick.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::runtime {

// `min_readable` is the compatibility PROMISE, made executable: raising it
// abandons every save older than that, so it does not move without a migration
// having been deleted on purpose.
inline constexpr aether::BlobFormat kSaveFormat{
    .magic = {aether::Byte{'H'}, aether::Byte{'F'}, aether::Byte{'S'},
              aether::Byte{'V'}},
    // v2 (H3) added the chain: the barn, buildings, the order board and the
    // purse. v3 (H5) added the coop AND the item-count prefix — see
    // kItemCountV2. v4 (H7) gave every building a KIND and a CELL: until then a
    // building had no position at all and the mill the player saw was an
    // authored mesh nothing connected to this table. v5 (sky world s4) added
    // the ARCHIPELAGO: which islands are owned and which one the player is on,
    // because the farm stopped being the world and became one island's zone.
    // v1 is still readable and always will be while `min_readable` says 1 —
    // raising that abandons every farm written before H3, which is a deliberate
    // act and not a tidy-up.
    // v6 (the second farm) appended the DORMANT ISLANDS: a length-prefixed
    // list of farm records for the islands not being stood on. An append like
    // every version before it, so a v5 file needs no special case - the farm it
    // holds is the island it says it is on, and there were never any others.
    .current = 6,
    .min_readable = 1,
    .name = "hearthfield save"};

// HOW MANY ITEMS v2 COULD POSSIBLY HAVE HELD. Frozen at four, forever.
//
// THIS CONSTANT IS THE BUG H5 FOUND. v2 wrote the barn as a bare run of
// `kItemCount` counts with no length, so the item catalogue's SIZE was part of
// the format's geometry — and appending `kEgg` would have made every v2 file on
// disk read one count too many, stealing four bytes from the BUILDING count and
// corrupting a section the barn code never touches.
//
// Append-only ids protect a stored index's MEANING. They say nothing about a
// stored table's LENGTH. Those are two separate promises and v2 only made the
// first. v3 writes the length, so this is the last time a new item costs a
// version.
inline constexpr aether::U32 kItemCountV2 = 4;
static_assert(content::kItemCount >= kItemCountV2,
              "items are append-only: a build with fewer items than v2 had "
              "cannot read the farms v2 wrote");

// Everything ADR-0106 calls part of the world, plus the other end of the
// offline delta.
struct SavedFarm {
  aether::I64 saved_at = 0;  // Unix seconds; app/ diffs it against now
  Tick tick = 0;
  aether::U64 rng_state = 0;
  aether::U32 columns = 0;  // THE SAVE SIZES THE BOARD, not the command line
  std::vector<Plot> plots;

  // v2. Defaulted here so a v1 file simply leaves them alone — the migration is
  // "read what is there", plus the one correction §3e of the H3 plan is about.
  Barn barn;
  std::vector<Building> buildings;
  OrderBoard board;
  // SPLIT at the second farm's n1: coin is the PLAYER's and follows them
  // between islands, owned land is the ISLAND's and stays behind. The wire
  // format is unchanged - coin still precedes owned, in the same two U32s v2
  // wrote - because v6 is a later task and n1 must not touch the format.
  Purse purse;
  Land land;

  // v3. Defaulted for the same reason the v2 block is: an older file simply
  // leaves it alone, and a farm that predates the coop gets one with no birds
  // in it rather than a refusal.
  Coop coop;

  // v5. Same rule again, and here the default is the whole migration: a farm
  // written before the sky world existed owns the hub and stands on it, which
  // is exactly where it was. Nothing is taken away and nothing is asked.
  Archipelago isles;

  // v6. The islands NOT being stood on — one record each, parallel arrays of id
  // and farm. Empty in a v5 file, which is the whole migration: a farm written
  // before there was anywhere else to go had nowhere else to be.
  //
  // ONE LEVEL ONLY. A dormant record's own `dormant` is always empty and the
  // decoder never fills it; `std::vector` of an incomplete type is legal since
  // C++17 and is what lets the record BE a SavedFarm rather than a near-copy of
  // one that could drift from it.
  std::vector<content::IslandId> dormant_ids;
  std::vector<SavedFarm> dormant;
};

// ONE farm into the stream, in the CURRENT version's layout. The inverse of
// ReadFarmBody and split out for the same reason — v6 writes several farms and
// two writers would be two things to remember.
inline void PutFarmBody(std::vector<aether::Byte>& payload,
                        const SavedFarm& farm) {
  // NO reserve: `reserve(28 + plots.size() * 19)` makes gcc 14 at -O3 emit a
  // bogus -Wfree-nonheap-object, and -Werror kills the row. Do not re-add it.
  aether::PutU64(payload, static_cast<aether::U64>(farm.saved_at));
  aether::PutU64(payload, farm.tick);
  aether::PutU64(payload, farm.rng_state);
  aether::PutU32(payload, farm.columns);
  aether::PutU32(payload, static_cast<aether::U32>(farm.plots.size()));
  for (const Plot& plot : farm.plots) {
    aether::PutU8(payload, static_cast<aether::U8>(plot.state));
    aether::PutU16(payload, plot.crop);
    aether::PutU64(payload, plot.sown_tick);
    aether::PutU64(payload, plot.ready_tick);
  }

  // ---- v2: the chain, appended after v1's payload -------------------------
  //
  // APPENDED, never interleaved. A v1 reader would stop at the plot table and
  // a v2 reader continues, which is what makes "read what is there" a viable
  // migration rule and what v3 will do again.
  aether::PutU32(payload, farm.barn.capacity);
  // v3: THE LENGTH, then the counts. The one line that makes adding an item a
  // data change instead of a format change (§3c of the H5 plan).
  aether::PutU32(payload, static_cast<aether::U32>(content::kItemCount));
  for (const aether::U32 count : farm.barn.counts) {
    aether::PutU32(payload, count);
  }
  aether::PutU32(payload, static_cast<aether::U32>(farm.buildings.size()));
  for (const Building& building : farm.buildings) {
    aether::PutU8(payload, building.queued);
    aether::PutU64(payload, building.done_tick);
    for (const content::RecipeId recipe : building.queue) {
      aether::PutU16(payload, recipe);
    }
    // v4: what it is and where it stands.
    aether::PutU16(payload, building.kind);
    aether::PutU16(payload, building.cell.col);
    aether::PutU16(payload, building.cell.row);
  }
  for (const Order& order : farm.board.slots) {
    aether::PutU16(payload, order.item);
    aether::PutU32(payload, order.count);
    aether::PutU32(payload, order.reward);
    aether::PutU8(payload, order.active ? 1 : 0);
  }
  aether::PutU64(payload, farm.board.next_refresh);
  aether::PutU32(payload, farm.purse.coin);
  aether::PutU32(payload, farm.land.owned);

  // ---- v3: the coop, appended after v2's payload -------------------------
  aether::PutU8(payload, farm.coop.animals);
  aether::PutU64(payload, farm.coop.fed_until);
  aether::PutU64(payload, farm.coop.next_lay);

  // ---- v5: the archipelago, appended after v3's ---------------------------
  //
  // TWO FIXED-WIDTH FIELDS AND NO LENGTH, on purpose: the unlocked set is a
  // bitmask, so this block's size does not depend on how many islands the build
  // has. That is the whole reason it is a mask — see kItemCountV2 above for
  // what a lengthless run of per-content-item data cost last time.
  aether::PutU32(payload, farm.isles.unlocked);
  aether::PutU32(payload, static_cast<aether::U32>(farm.isles.current));
}

[[nodiscard]] inline std::vector<aether::Byte> EncodeSave(
    const SavedFarm& farm) {
  std::vector<aether::Byte> payload;
  // THE LIVE ISLAND FIRST, in the exact bytes v5 wrote. v6 is an APPEND like
  // every version before it — which is what keeps "read what is there" the
  // whole migration and why a v5 file needs no special case at all.
  PutFarmBody(payload, farm);

  // ---- v6: the DORMANT islands, appended after v5's -----------------------
  //
  // THE LENGTH FIRST, and it is the one line this block turns on.
  // `kItemCountV2` records what a lengthless run of per-content data cost when
  // the catalogue grew; this is that hazard with VARIABLE-LENGTH elements,
  // where a reader that guessed the count would not merely read the wrong
  // number of items but resynchronise in the middle of a plot table.
  //
  // Each record is `U16 island id` then a full farm body — the SAME body the
  // live island uses, because one codec cannot drift from itself. A dormant
  // record's shared fields (coin, the archipelago) are written and IGNORED on
  // read: the live block above is the only authority for those, and duplicating
  // them costs thirteen bytes an island against the certainty that the two
  // layouts stay identical.
  aether::PutU32(payload, static_cast<aether::U32>(farm.dormant.size()));
  for (aether::Usize i = 0; i < farm.dormant.size(); ++i) {
    aether::PutU16(payload, farm.dormant_ids[i]);
    PutFarmBody(payload, farm.dormant[i]);
  }
  return aether::EncodeBlob(kSaveFormat, payload);
}

// ONE farm out of the stream, at a given format version.
//
// SPLIT OUT OF DecodeSave AT v6, and the reason is drift rather than tidiness:
// v6 stores several farms, and a second decoder for the dormant ones would be a
// second thing to remember when a field is added — the same "three lists must
// agree" failure `world_view.hpp` names, in a fourth place. There is one reader
// and one writer, and every validation below therefore applies to a dormant
// island exactly as it does to the live one.
//
// Every refusal names what is wrong, because the alternative to loading a
// player's farm is telling them precisely why not.
[[nodiscard]] inline aether::Result<SavedFarm> ReadFarmBody(
    aether::ByteReader& reader, aether::U32 version) {
  // v1's payload first — every version starts with it, which is what makes
  // "read what is there, then correct" a migration rule rather than a special
  // case per version. The v2 branch is at the bottom, after the plot table.
  constexpr aether::Usize kHeaderBytes = 8 + 8 + 8 + 4 + 4;
  if (!reader.Ok(kHeaderBytes)) {
    return aether::Fail(aether::Errc::kParseError,
                        "hearthfield save: truncated before the farm header");
  }

  SavedFarm farm;
  farm.saved_at = static_cast<aether::I64>(reader.U64At());
  farm.tick = reader.U64At();
  farm.rng_state = reader.U64At();
  farm.columns = reader.U32At();
  const aether::U32 count = reader.U32At();

  // The board is square, and a file that says otherwise is corrupt rather than
  // merely surprising — believing it would index off the end of the grid.
  if (farm.columns == 0 || farm.columns > 64 ||
      count != farm.columns * farm.columns) {
    return aether::Fail(aether::Errc::kParseError,
                        "hearthfield save: " + std::to_string(count) +
                            " plots does not match a " +
                            std::to_string(farm.columns) + "-column board");
  }
  constexpr aether::Usize kPlotBytes = 1 + 2 + 8 + 8;
  if (!reader.Ok(count * kPlotBytes)) {
    return aether::Fail(aether::Errc::kParseError,
                        "hearthfield save: truncated inside the plot table");
  }

  farm.plots.resize(count);
  for (aether::U32 i = 0; i < count; ++i) {
    Plot& plot = farm.plots[i];
    const aether::U8 state = reader.U8At();
    if (state > static_cast<aether::U8>(PlotState::kReady)) {
      return aether::Fail(aether::Errc::kParseError,
                          "hearthfield save: plot " + std::to_string(i) +
                              " has unknown state " + std::to_string(state));
    }
    plot.state = static_cast<PlotState>(state);
    plot.crop = reader.U16At();
    plot.sown_tick = reader.U64At();
    plot.ready_tick = reader.U64At();

    // THE FIRST FAILURE A CONTENT CHANGE PRODUCES, and the one the format
    // cannot see. Crop ids are indices, so removing a crop renumbers every one
    // after it and a plot silently becomes a different plant. Refused with the
    // id, never guessed at — and the catalogue is append-only for this reason.
    if (plot.state != PlotState::kEmpty && !content::CropExists(plot.crop)) {
      return aether::Fail(
          aether::Errc::kUnsupported,
          "hearthfield save: plot " + std::to_string(i) + " grows crop " +
              std::to_string(plot.crop) +
              ", which this build's catalogue does not have — it was written "
              "by a build with different content");
    }
    // A crop cannot ripen before it was sown. Cheap, and it catches a file
    // whose fields were written in the wrong order far earlier than a farm
    // that never finishes growing would.
    if (plot.state != PlotState::kEmpty && plot.ready_tick < plot.sown_tick) {
      return aether::Fail(aether::Errc::kParseError,
                          "hearthfield save: plot " + std::to_string(i) +
                              " ripens before it was sown");
    }
  }

  // ---- THE MIGRATION -------------------------------------------------------
  //
  // The first one this repo has written. H2 shipped v1 and said out loud that
  // it could not test the upgrade branch because there was no v2; this is it.
  //
  // The correction that matters is `unlocked`. A v1 farm has no concept of
  // locked land, so every plot in it was the player's — defaulting to the
  // starting handful would CONFISCATE their board on upgrade, which is a
  // one-line mistake and the worst possible first impression of a save system.
  if (version < 2) {
    farm.land.owned = count;
    farm.buildings.resize(1);  // the mill everyone starts with
    // ...standing where the authored mill stood, same as the v3 migration
    // below. Default-constructing its cell would put it in the ring's corner,
    // which is a legal cell and the wrong one.
    farm.buildings[0].kind = content::kMillKind;
    farm.buildings[0].cell = kStarterMillCell;
    return farm;
  }

  // ---- the barn, and the LENGTH question --------------------------------
  //
  // v2 wrote a bare run of counts, so how many there are is decided by WHICH
  // VERSION WROTE THE FILE, not by the build reading it. Getting this from
  // `kItemCount` is the bug §3c of the H5 plan is about: it grows, the file
  // does not, and the overrun lands in the building table.
  if (!reader.Ok(4)) {
    return aether::Fail(aether::Errc::kParseError,
                        "hearthfield save: v2 but truncated before the barn");
  }
  farm.barn.capacity = reader.U32At();

  aether::U32 stored_items = kItemCountV2;
  if (version >= 3) {
    if (!reader.Ok(4)) {
      return aether::Fail(
          aether::Errc::kParseError,
          "hearthfield save: v3 but truncated before the item count");
    }
    stored_items = reader.U32At();
    // A file from a LATER build, holding items this one has never heard of.
    // Refused rather than truncated: silently dropping the tail would look
    // like the player's barn had been emptied, and re-saving would make the
    // loss permanent.
    if (stored_items > content::kItemCount) {
      return aether::Fail(aether::Errc::kUnsupported,
                          "hearthfield save: it holds " +
                              std::to_string(stored_items) +
                              " kinds of item and this build knows " +
                              std::to_string(content::kItemCount) +
                              " — it was written by a newer build");
    }
  }
  if (!reader.Ok((stored_items * 4) + 4)) {
    return aether::Fail(aether::Errc::kParseError,
                        "hearthfield save: truncated inside the barn");
  }
  // Only what the file carries. Items added since it was written stay 0, which
  // is exactly right: the player never had any.
  for (aether::U32 i = 0; i < stored_items; ++i) {
    farm.barn.counts[i] = reader.U32At();
  }

  const aether::U32 building_count = reader.U32At();
  if (building_count > 64) {
    return aether::Fail(aether::Errc::kParseError,
                        "hearthfield save: " + std::to_string(building_count) +
                            " buildings is not a farm");
  }
  // v4 appended kind + col + row. Sized from the FILE's version, not from the
  // current struct: reading a v3 farm with v4's stride would walk six bytes per
  // building into the order board.
  const aether::Usize kBuildingBytes =
      1 + 8 + (2 * kQueueCapacity) + (version >= 4 ? 6 : 0);
  constexpr aether::Usize kBoardBytes =
      ((2 + 4 + 4 + 1) * kOrderSlots) + 8 + 4 + 4;
  if (!reader.Ok((building_count * kBuildingBytes) + kBoardBytes)) {
    return aether::Fail(aether::Errc::kParseError,
                        "hearthfield save: v2 but truncated inside the chain");
  }

  farm.buildings.resize(building_count);
  aether::U32 index = 0;
  for (Building& building : farm.buildings) {
    building.queued = reader.U8At();
    building.done_tick = reader.U64At();
    for (content::RecipeId& recipe : building.queue) {
      recipe = reader.U16At();
    }
    if (version >= 4) {
      building.kind = reader.U16At();
      building.cell.col = reader.U16At();
      building.cell.row = reader.U16At();
      if (!content::BuildingKindExists(building.kind)) {
        return aether::Fail(aether::Errc::kParseError,
                            "hearthfield save: building kind " +
                                std::to_string(building.kind) +
                                " is not in the catalogue");
      }
    } else {
      // MIGRATION. Before v4 a farm had exactly one building — "the mill
      // everyone starts with" — and it had no position, because the mill the
      // player saw was authored into farm.world.json. It goes where that
      // authored mill stood, which is what kStarterMillCell records. A second
      // building cannot exist in such a file, so the fallback below is
      // unreachable by construction and is a refusal rather than a guess.
      building.kind = content::kMillKind;
      if (index == 0) {
        building.cell = kStarterMillCell;
      } else {
        return aether::Fail(aether::Errc::kParseError,
                            "hearthfield save: a pre-v4 farm claims more than "
                            "one building, which no version could write");
      }
    }
    ++index;
    if (building.queued > kQueueCapacity) {
      return aether::Fail(aether::Errc::kParseError,
                          "hearthfield save: a building claims " +
                              std::to_string(building.queued) +
                              " queued, over the capacity of " +
                              std::to_string(kQueueCapacity));
    }
    // Same class of check as the crop id, one level down: a recipe removed from
    // the catalogue would leave a mill grinding something that no longer
    // exists. Only the QUEUED entries matter; the tail is uninitialised
    // padding.
    for (aether::U8 k = 0; k < building.queued; ++k) {
      if (!content::RecipeExists(building.queue[k])) {
        return aether::Fail(aether::Errc::kUnsupported,
                            "hearthfield save: a building is making recipe " +
                                std::to_string(building.queue[k]) +
                                ", which this build does not have");
      }
    }
  }

  for (Order& order : farm.board.slots) {
    order.item = reader.U16At();
    order.count = reader.U32At();
    order.reward = reader.U32At();
    order.active = reader.U8At() != 0;
    if (order.active && !content::ItemExists(order.item)) {
      return aether::Fail(aether::Errc::kUnsupported,
                          "hearthfield save: an order wants item " +
                              std::to_string(order.item) +
                              ", which this build does not have");
    }
  }
  farm.board.next_refresh = reader.U64At();
  farm.purse.coin = reader.U32At();
  farm.land.owned = reader.U32At();

  // ---- v3: the coop ------------------------------------------------------
  //
  // Same migration rule as v2's: read what is there. A v2 farm gets a coop with
  // no birds, which is what it had.
  if (version >= 3) {
    constexpr aether::Usize kCoopBytes = 1 + 8 + 8;
    if (!reader.Ok(kCoopBytes)) {
      return aether::Fail(aether::Errc::kParseError,
                          "hearthfield save: v3 but truncated before the coop");
    }
    farm.coop.animals = reader.U8At();
    farm.coop.fed_until = reader.U64At();
    farm.coop.next_lay = reader.U64At();
  }

  // ---- v5: the archipelago ------------------------------------------------
  //
  // A v4 farm keeps the default — the hub, owned and stood upon — which is
  // where it already was. NOT TRUSTED AS WRITTEN: IslandsFromSave drops bits
  // naming islands this build does not have and sends a player standing on a
  // locked or absent island back to the hub, because the alternative is a farm
  // that loads into nowhere.
  if (version >= 5) {
    constexpr aether::Usize kIslesBytes = 4 + 4;
    if (!reader.Ok(kIslesBytes)) {
      return aether::Fail(
          aether::Errc::kParseError,
          "hearthfield save: v5 but truncated before the archipelago");
    }
    const aether::U32 unlocked = reader.U32At();
    const aether::U32 current = reader.U32At();
    farm.isles = IslandsFromSave(unlocked, current);
  }
  if (farm.land.owned > count) {
    return aether::Fail(aether::Errc::kParseError,
                        "hearthfield save: " + std::to_string(farm.land.owned) +
                            " plots owned on a board of " +
                            std::to_string(count));
  }
  return farm;
}

// A fingerprint of one farm RECORD — over the bytes the format would write for
// it, not over its fields.
//
// THAT CHOICE IS THE WHOLE POINT AND IT AVOIDS A FOURTH LIST. `world_view.hpp`
// warns that three things must agree — what Restore restores, what the save
// carries, and what Digest hashes — and that forgetting the third makes the
// round-trip test pass while a field is not persisted. A hand-written
// field-by-field digest for dormant records would have been a FOURTH list with
// the same failure mode and no test that could see it.
//
// Hashing `PutFarmBody`'s output cannot drift from the format, because it IS
// the format. A field the save stops carrying changes this value; a field the
// save never carried was never in it. The cost is an encode per call, which is
// fine for something used at save time and in tests rather than per frame.
[[nodiscard]] inline aether::U64 DigestOfRecord(const SavedFarm& farm) {
  std::vector<aether::Byte> bytes;
  PutFarmBody(bytes, farm);
  aether::U64 hash = 0xCBF29CE484222325ULL;  // FNV-1a, as WorldView::Digest
  for (const aether::Byte byte : bytes) {
    hash ^= static_cast<aether::U64>(byte);
    hash *= 0x100000001B3ULL;
  }
  return hash;
}

// Every refusal names what is wrong, because the alternative to loading a
// player's farm is telling them precisely why not.
[[nodiscard]] inline aether::Result<SavedFarm> DecodeSave(
    std::span<const aether::Byte> bytes) {
  auto blob = aether::DecodeBlob(kSaveFormat, bytes);
  if (!blob) {
    return std::unexpected(blob.error());
  }
  aether::ByteReader reader(blob->payload);
  auto farm = ReadFarmBody(reader, blob->version);
  if (!farm) {
    return std::unexpected(farm.error());
  }

  // ---- v6: the dormant islands --------------------------------------------
  //
  // A pre-v6 file takes no branch and keeps the empty default, which IS the
  // migration: the farm it holds becomes the island it says it is standing on,
  // and there were never any others.
  if (blob->version >= 6) {
    if (!reader.Ok(4)) {
      return aether::Fail(
          aether::Errc::kParseError,
          "hearthfield save: v6 but truncated before the island count");
    }
    const aether::U32 dormant = reader.U32At();
    // A COUNT IS NOT A PROMISE, and 32 is the ceiling by construction: the
    // unlocked set is a U32 bitmask, so no more islands can exist
    // (content::islands.hpp static_asserts the other half of it).
    //
    // BE HONEST ABOUT WHAT THIS BOUND BUYS: nothing is reserved from the count,
    // so an inflated one would be caught anyway by the first truncated record
    // a few lines below — mutation-testing this bound away leaves every test
    // still passing, for that reason. What it buys is the ERROR, not the
    // safety: "it claims 65535 islands" names the corruption, where
    // "truncated before dormant island 0" describes a symptom and points at
    // the wrong end of the file. Keep it, and do not mistake it for a guard.
    if (dormant > 32) {
      return aether::Fail(aether::Errc::kParseError,
                          "hearthfield save: it claims " +
                              std::to_string(dormant) +
                              " dormant islands and a mask can hold 32");
    }
    for (aether::U32 i = 0; i < dormant; ++i) {
      if (!reader.Ok(2)) {
        return aether::Fail(
            aether::Errc::kParseError,
            "hearthfield save: truncated before dormant island " +
                std::to_string(i));
      }
      const auto id = static_cast<content::IslandId>(reader.U16At());
      auto record = ReadFarmBody(reader, blob->version);
      if (!record) {
        return std::unexpected(record.error());
      }
      // NOT TRUSTED AS WRITTEN, the same rule IslandsFromSave follows: a record
      // for an island this build does not have is DROPPED rather than kept,
      // because everything downstream indexes kIslands by that id. A file from
      // a later build loses those farms — which is the honest outcome, since
      // this build cannot show them anywhere.
      if (!content::IslandExists(id)) {
        continue;
      }
      farm->dormant_ids.push_back(id);
      farm->dormant.push_back(std::move(*record));
    }
  }
  return farm;
}

// ---- the world <-> the file ------------------------------------------------

[[nodiscard]] inline SavedFarm CaptureFarm(const WorldView& world,
                                           aether::I64 saved_at,
                                           aether::U32 columns) {
  const std::span<const Plot> plots = world.Plots();
  const std::span<const Building> buildings = world.Buildings();
  return SavedFarm{
      .saved_at = saved_at,
      .tick = world.Now(),
      .rng_state = world.Random().State(),
      .columns = columns,
      .plots = std::vector<Plot>(plots.begin(), plots.end()),
      .barn = world.TheBarn(),
      .buildings = std::vector<Building>(buildings.begin(), buildings.end()),
      .board = world.Board(),
      .purse = world.ThePurse(),
      .land = world.TheLand(),
      .coop = world.TheCoop(),
      .isles = world.Isles()};
}

inline void RestoreFarm(WorldView& world, const SavedFarm& farm) {
  world.Restore(farm.tick, farm.rng_state, farm.plots, farm.barn,
                farm.buildings, farm.board, farm.purse, farm.land, farm.coop,
                farm.isles);
}

// Ticks that passed while the game was shut, from two wall-clock readings.
//
// THE CLAMP IS THE WHOLE FUNCTION. system_clock is the only clock that survives
// a restart and it is the one the player owns, so both directions are hostile:
// backwards (NTP, DST, a manual edit) would underflow an unsigned delta into
// billions of years, and far forwards is the genre's oldest exploit. Kept here
// rather than in app/ so it is testable without a process.
inline constexpr aether::I64 kMaxOfflineSeconds = 30LL * 24 * 60 * 60;

[[nodiscard]] constexpr aether::U32 OfflineTicksBetween(aether::I64 saved_at,
                                                        aether::I64 now) {
  const aether::I64 elapsed = now - saved_at;
  if (elapsed <= 0) {
    return 0;
  }
  const aether::I64 capped =
      elapsed > kMaxOfflineSeconds ? kMaxOfflineSeconds : elapsed;
  return static_cast<aether::U32>(capped *
                                  static_cast<aether::I64>(kTicksPerSecond));
}

}  // namespace hearthfield::runtime
