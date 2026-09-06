// The save file: the first artefact this repo has written that must outlive
// the process AND survive the next build.
//
// Everything here is headless — the format is pure data over the sim's own
// state, which is the reason it lives in `runtime` rather than in app/.
#include "hf/runtime/save.hpp"

#include <doctest/doctest.h>

#include <vector>

#include "hf/content/crops.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/runtime/game_world.hpp"
#include "hf/runtime/replay.hpp"

using namespace aether;
using hearthfield::plots::PlotsSystem;
using hearthfield::runtime::DecodeSave;
using hearthfield::runtime::EncodeSave;
using hearthfield::runtime::GameWorld;
using hearthfield::runtime::LatchedInput;
using hearthfield::runtime::OfflineTicksBetween;
using hearthfield::runtime::Plot;
using hearthfield::runtime::PlotState;
using hearthfield::runtime::SavedFarm;
namespace content = hearthfield::content;
namespace runtime = hearthfield::runtime;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;
constexpr I64 kSavedAt = 1'700'000'000;  // an ordinary Unix timestamp

// A farm with a plot in every state, which is what makes the round-trip
// assertion cover the whole table rather than the empty case.
[[nodiscard]] GameWorld PlayedFarm() {
  GameWorld world(/*seed=*/0xC0FFEE, /*plot_count=*/9);
  PlotsSystem plots;
  world.AddSystem(plots);
  const auto grow = static_cast<U32>(runtime::TicksFromSeconds(
      content::CropById(content::kWheat).grow_seconds));
  world.Step(LatchedInput{.tap = true, .hovered = 0}, kDt);  // -> ripe below
  world.Step(LatchedInput{.tap = true, .hovered = 4}, kDt);  // -> mid-growth
  world.Step(LatchedInput{.offline_ticks = grow, .tap = false}, kDt);
  world.Step(LatchedInput{.tap = true, .hovered = 7}, kDt);  // freshly sown
  (void)world.World().Random().NextU64();  // move the RNG off its seed
  return world;
}

}  // namespace

TEST_CASE("A SAVED FARM ROUND-TRIPS TO AN IDENTICAL WORLD") {
  // The digest is the assertion because it covers exactly what ADR-0106 says
  // is the world: the tick, the RNG state and the plot table. If the format
  // ever stops carrying one of the three, this fails rather than the loss
  // being noticed the first time a reloaded farm behaves differently.
  GameWorld live = PlayedFarm();
  const U64 before = live.World().Digest();

  const std::vector<Byte> bytes =
      EncodeSave(runtime::CaptureFarm(live.World(), kSavedAt, 3));
  auto loaded = DecodeSave(bytes);
  REQUIRE(loaded.has_value());
  CHECK(loaded->saved_at == kSavedAt);
  CHECK(loaded->columns == 3);
  CHECK(loaded->plots.size() == 9);

  GameWorld restored(/*seed=*/0, /*plot_count=*/9);
  runtime::RestoreFarm(restored.World(), *loaded);
  CHECK(restored.World().Digest() == before);
}

TEST_CASE("the RNG STATE is part of the file, not part of the seed") {
  // Spec §4.4: a farm reloaded with a fresh RNG is a different farm. The seed
  // below is deliberately wrong on the restoring side, so only a state that
  // actually travelled in the file can make the two agree.
  GameWorld live = PlayedFarm();
  for (int i = 0; i < 5; ++i) {
    (void)live.World().Random().NextU64();
  }
  const U64 saved_state = live.World().Random().State();

  auto loaded =
      DecodeSave(EncodeSave(runtime::CaptureFarm(live.World(), kSavedAt, 3)));
  REQUIRE(loaded.has_value());
  CHECK(loaded->rng_state == saved_state);

  // Two worlds restored from the same file, seeded DIFFERENTLY, must produce
  // the same numbers — the seed they were built with is irrelevant once the
  // state has travelled.
  GameWorld a(/*seed=*/12345, 9);
  GameWorld b(/*seed=*/999, 9);
  runtime::RestoreFarm(a.World(), *loaded);
  runtime::RestoreFarm(b.World(), *loaded);
  for (int i = 0; i < 5; ++i) {
    CHECK(a.World().Random().NextU64() == b.World().Random().NextU64());
  }

  // And the state must genuinely have come from the FILE: a world merely
  // constructed with one of those seeds draws something else entirely. Without
  // this the case above would pass on a Restore that ignored rng_state.
  GameWorld unrestored(/*seed=*/12345, 9);
  GameWorld restored_again(/*seed=*/12345, 9);
  runtime::RestoreFarm(restored_again.World(), *loaded);
  CHECK(unrestored.World().Random().NextU64() !=
        restored_again.World().Random().NextU64());
}

TEST_CASE("EVERY REFUSAL NAMES WHAT IS WRONG") {
  const std::vector<Byte> good =
      EncodeSave(runtime::CaptureFarm(PlayedFarm().World(), kSavedAt, 3));

  SUBCASE("a file that is not one of ours") {
    std::vector<Byte> bad = good;
    bad[0] = Byte{'X'};
    const auto out = DecodeSave(bad);
    REQUIRE_FALSE(out.has_value());
    CHECK(out.error().message.find("not an aether") != std::string::npos);
  }
  SUBCASE("truncated, at every length") {
    // Every prefix must be refused rather than half-loaded. A loader that
    // reads what it can leaves a farm missing its last rows and no error.
    for (Usize n = 0; n < good.size(); ++n) {
      CAPTURE(n);
      CHECK_FALSE(DecodeSave(std::span(good).first(n)).has_value());
    }
    CHECK(DecodeSave(good).has_value());
  }
  SUBCASE("written by a NEWER build") {
    std::vector<Byte> newer = good;
    newer[4] = Byte{99};  // the version field, little-endian
    const auto out = DecodeSave(newer);
    REQUIRE_FALSE(out.has_value());
    CHECK(out.error().code == Errc::kUnsupported);
    // The message must name BOTH numbers: the reader has to decide whether to
    // upgrade the game or go find an older one, and cannot from "unsupported".
    CHECK(out.error().message.find("99") != std::string::npos);
    CHECK(out.error().message.find("upgrade") != std::string::npos);
  }
  SUBCASE("a plot count that disagrees with the board") {
    std::vector<Byte> wrong = good;
    wrong[8 + 24] = Byte{5};  // columns: 3 -> 5, while 9 plots follow
    const auto out = DecodeSave(wrong);
    REQUIRE_FALSE(out.has_value());
    CHECK(out.error().message.find("column") != std::string::npos);
  }
}

TEST_CASE("A SAVE NAMING A CROP THIS BUILD LACKS IS REFUSED, NOT GUESSED") {
  // The first failure a content change produces, and the one the format cannot
  // see: crop ids are indices, so removing a crop renumbers every one after it
  // and a plot silently becomes a different plant. The catalogue is
  // append-only for this reason, and this is the guard behind that rule.
  SavedFarm farm{.saved_at = kSavedAt,
                 .tick = 100,
                 .rng_state = 7,
                 .columns = 1,
                 .plots = {Plot{.state = PlotState::kGrowing,
                                .crop = 999,
                                .sown_tick = 1,
                                .ready_tick = 50}}};
  const auto out = DecodeSave(EncodeSave(farm));
  REQUIRE_FALSE(out.has_value());
  CHECK(out.error().code == Errc::kUnsupported);
  CHECK(out.error().message.find("999") != std::string::npos);
}

TEST_CASE("A SAVE SURVIVES A CROP BEING ADDED TO THE CATALOGUE") {
  // Spec CHECK 6, as far as it can be proven today. `wheat` was id 0 when the
  // catalogue held one crop and is still id 0 now that it holds two, so a farm
  // written then still says wheat — which is what append-only buys.
  //
  // What this does NOT prove is the v1 -> v2 MIGRATION branch: that needs two
  // real format versions and this milestone ships the first. Said out loud
  // rather than counted as passing.
  REQUIRE(content::kCrops.size() >= 2);
  REQUIRE(content::kWheat == 0);

  SavedFarm old_farm{.saved_at = kSavedAt,
                     .tick = 500,
                     .rng_state = 3,
                     .columns = 2,
                     .plots = {Plot{.state = PlotState::kReady,
                                    .crop = content::kWheat,
                                    .sown_tick = 1,
                                    .ready_tick = 400},
                               Plot{}, Plot{}, Plot{}}};
  auto loaded = DecodeSave(EncodeSave(old_farm));
  REQUIRE(loaded.has_value());
  CHECK(loaded->plots[0].crop == content::kWheat);
  CHECK(content::CropById(loaded->plots[0].crop).name == "wheat");

  SUBCASE("and the new crop is usable in a new save") {
    SavedFarm fresh = old_farm;
    fresh.plots[1] = Plot{.state = PlotState::kGrowing,
                          .crop = content::kCorn,
                          .sown_tick = 10,
                          .ready_tick = 900};
    auto out = DecodeSave(EncodeSave(fresh));
    REQUIRE(out.has_value());
    CHECK(content::CropById(out->plots[1].crop).name == "corn");
  }
}

TEST_CASE("THE WALL-CLOCK DELTA IS CLAMPED IN BOTH DIRECTIONS") {
  // system_clock is the only clock that survives a restart, and it is the one
  // the player owns. Both directions are hostile and they are not symmetric.
  SUBCASE("an ordinary ten minutes converts exactly") {
    CHECK(OfflineTicksBetween(kSavedAt, kSavedAt + 600) ==
          600 * runtime::kTicksPerSecond);
  }
  SUBCASE("A CLOCK THAT WENT BACKWARDS IS NOT A BILLION-YEAR ABSENCE") {
    // An NTP correction or a DST change. Subtracting into an unsigned delta is
    // the bug this exists to prevent, and the symptom would be every crop in
    // the farm instantly ripe.
    CHECK(OfflineTicksBetween(kSavedAt, kSavedAt - 1) == 0);
    CHECK(OfflineTicksBetween(kSavedAt, 0) == 0);
    CHECK(OfflineTicksBetween(kSavedAt, kSavedAt) == 0);
  }
  SUBCASE("A CLOCK SET FAR FORWARD IS CAPPED, which is also the exploit") {
    const U32 capped =
        OfflineTicksBetween(kSavedAt, kSavedAt + 10 * 365 * 86400);
    CHECK(capped == static_cast<U32>(runtime::kMaxOfflineSeconds *
                                     runtime::kTicksPerSecond));
    // And the cap must fit the U32 the latched field is — 30 days at 60 Hz is
    // 155.5 M ticks, comfortably inside it.
    CHECK(capped < 0xFFFFFFFFu);
  }
}

TEST_CASE("CHECK 5: A SESSION REPLAYS ACROSS A SAVE AND AN OFFLINE GAP") {
  // The claim the whole tick model exists to support: the wall clock was read
  // OUTSIDE the step, its result was recorded like any other latched value, so
  // a replay reproduces overnight growth to the tick without a clock anywhere
  // in it.
  const U32 away = OfflineTicksBetween(kSavedAt, kSavedAt + 3600);
  const std::vector<LatchedInput> script = {
      LatchedInput{.tap = true, .hovered = 0},
      LatchedInput{.tap = true, .hovered = 3},
      LatchedInput{.offline_ticks = away},  // the gap, as data
      LatchedInput{},
      LatchedInput{.tap = true, .hovered = 0},  // harvest what ripened
  };

  const auto play = [&](bool through_a_file) {
    GameWorld world(/*seed=*/42, 9);
    PlotsSystem plots;
    world.AddSystem(plots);
    runtime::ReplayRecorder recorder;
    for (Usize i = 0; i < script.size(); ++i) {
      recorder.Record(script[i]);
      world.Step(script[i], kDt);
      if (through_a_file && i == 1) {
        // Quit and relaunch in the middle of the session.
        auto loaded = DecodeSave(
            EncodeSave(runtime::CaptureFarm(world.World(), kSavedAt, 3)));
        REQUIRE(loaded.has_value());
        runtime::RestoreFarm(world.World(), *loaded);
      }
    }
    return world.World().Digest();
  };

  CHECK(play(/*through_a_file=*/true) == play(/*through_a_file=*/false));
}

// ---- the migration ----------------------------------------------------------

#include <filesystem>
#include <fstream>
#include <string>

namespace {

// The v1 fixture, written by the build BEFORE H3 added the chain. Its whole
// value is that no current code produced it: regenerating it would turn this
// into a round-trip, which passes by construction and proves nothing — the same
// bargain tests/assets/recordings/ already records.
[[nodiscard]] std::vector<Byte> ReadV1Fixture() {
  const std::filesystem::path path =
      std::filesystem::path(AETHER_TEST_ASSET_DIR) / "saves" /
      "v1_farm_3x3.hfsv";
  std::ifstream file(path, std::ios::binary);
  REQUIRE_MESSAGE(file.good(), "missing v1 fixture: " << path.string());
  // Through `char` and then converted: std::byte has no implicit conversion
  // from char, which is the whole point of it being a distinct type.
  const std::string raw((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
  std::vector<Byte> bytes;
  bytes.reserve(raw.size());
  for (const char c : raw) {
    bytes.push_back(static_cast<Byte>(c));
  }
  return bytes;
}

}  // namespace

TEST_CASE("A v1 SAVE LOADS IN THE v2 BUILD, AND KEEPS ITS WHOLE BOARD") {
  // THE FIRST REAL MIGRATION IN THIS REPO. H2 shipped v1 and said out loud that
  // it could not test the upgrade branch, because there was no v2. This is it.
  const std::vector<Byte> v1 = ReadV1Fixture();
  REQUIRE_FALSE(v1.empty());

  auto farm = DecodeSave(v1);
  if (!farm) {
    FAIL("the v1 fixture would not load: ", farm.error().message);
  }
  REQUIRE(farm.has_value());
  CHECK(farm->columns == 3);
  CHECK(farm->plots.size() == 9);

  // THE CORRECTION THAT MATTERS. v1 had no concept of locked land, so every
  // plot in that file was the player's. Defaulting to the starting handful
  // would confiscate two thirds of their farm on upgrade — a one-line mistake,
  // and the worst possible first impression of a save system.
  CHECK(farm->land.owned == 9);
  CHECK(farm->purse.coin == 0);
  CHECK(farm->barn.Total() == 0);
  // And it comes back with the mill everybody starts with, rather than a farm
  // that can never refine anything.
  CHECK(farm->buildings.size() == 1);
  CHECK(farm->buildings[0].queued == 0);

  SUBCASE("its crops survived the trip") {
    U32 growing = 0;
    for (const Plot& plot : farm->plots) {
      if (plot.state != PlotState::kEmpty) {
        ++growing;
        CHECK(content::CropExists(plot.crop));
      }
    }
    CHECK(growing == 9);  // it was written with --sow
  }

  SUBCASE("and re-saving it writes the CURRENT version") {
    // The upgrade is one-way and silent, which is what a player wants: they
    // opened the game and it worked.
    //
    // Against kSaveFormat.current rather than a literal: this line said `== 2`
    // until v3 existed, and a version bump should not have to remember to come
    // and edit an assertion whose point is "the newest one".
    const std::vector<Byte> current = EncodeSave(*farm);
    CHECK(static_cast<U8>(current[4]) == runtime::kSaveFormat.current);
    auto again = DecodeSave(current);
    REQUIRE(again.has_value());
    CHECK(again->land.owned == 9);
    CHECK(again->buildings.size() == 1);
  }
}

TEST_CASE("the v2 chain round-trips, field by field") {
  // No fixture needed: a hand-built farm exercises every v2 field.
  SavedFarm farm{.saved_at = kSavedAt,
                 .tick = 9000,
                 .rng_state = 0xABCDEF,
                 .columns = 2,
                 .plots = {Plot{}, Plot{}, Plot{}, Plot{}}};
  farm.barn.capacity = 77;
  farm.barn.counts[content::kFlour] = 5;
  farm.barn.counts[content::kWheatItem] = 3;
  farm.buildings.resize(2);
  farm.buildings[0].queued = 2;
  farm.buildings[0].done_tick = 12345;
  farm.buildings[0].queue[0] = content::kGrindCorn;
  farm.buildings[0].queue[1] = content::kGrindWheat;
  farm.board.slots[2] = runtime::Order{
      .item = content::kMeal, .count = 4, .reward = 80, .active = true};
  farm.board.next_refresh = 54321;
  farm.purse.coin = 999;
  farm.land.owned = 3;

  auto out = DecodeSave(EncodeSave(farm));
  REQUIRE(out.has_value());
  CHECK(out->barn.capacity == 77);
  CHECK(out->barn.Of(content::kFlour) == 5);
  CHECK(out->barn.Of(content::kWheatItem) == 3);
  REQUIRE(out->buildings.size() == 2);
  CHECK(out->buildings[0].queued == 2);
  CHECK(out->buildings[0].done_tick == 12345);
  CHECK(out->buildings[0].queue[0] == content::kGrindCorn);
  CHECK(out->buildings[0].queue[1] == content::kGrindWheat);
  CHECK(out->board.slots[2].active);
  CHECK(out->board.slots[2].reward == 80);
  CHECK(out->board.next_refresh == 54321);
  CHECK(out->purse.coin == 999);
  CHECK(out->land.owned == 3);
}

TEST_CASE("v2 refuses a mill grinding a recipe this build does not have") {
  SavedFarm farm{.saved_at = kSavedAt,
                 .tick = 1,
                 .rng_state = 1,
                 .columns = 1,
                 .plots = {Plot{}}};
  farm.buildings.resize(1);
  farm.buildings[0].queued = 1;
  farm.buildings[0].queue[0] = 500;  // no such recipe
  const auto out = DecodeSave(EncodeSave(farm));
  REQUIRE_FALSE(out.has_value());
  CHECK(out.error().code == Errc::kUnsupported);
  CHECK(out.error().message.find("500") != std::string::npos);
}

TEST_CASE("v2 refuses owning more plots than the board has") {
  SavedFarm farm{.saved_at = kSavedAt,
                 .tick = 1,
                 .rng_state = 1,
                 .columns = 2,
                 .plots = {Plot{}, Plot{}, Plot{}, Plot{}}};
  farm.land.owned = 9;
  const auto out = DecodeSave(EncodeSave(farm));
  REQUIRE_FALSE(out.has_value());
  CHECK(out.error().message.find("owned") != std::string::npos);
}

// ---- v3, and the LENGTH problem that made it necessary ----------------------

namespace {

// The v2 fixture, written by the FOUR-ITEM build — before `kEgg` existed. Same
// bargain as the v1 one: no current code produced it, which is the only reason
// it can catch what it catches.
[[nodiscard]] std::vector<Byte> ReadV2Fixture() {
  const std::filesystem::path path =
      std::filesystem::path(AETHER_TEST_ASSET_DIR) / "saves" /
      "v2_farm_3x3.hfsv";
  std::ifstream file(path, std::ios::binary);
  REQUIRE_MESSAGE(file.good(), "missing v2 fixture: " << path.string());
  const std::string raw((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
  std::vector<Byte> bytes;
  bytes.reserve(raw.size());
  for (const char c : raw) {
    bytes.push_back(static_cast<Byte>(c));
  }
  return bytes;
}

}  // namespace

TEST_CASE("A v2 SAVE SURVIVES THE ITEM THAT WAS ADDED AFTER IT") {
  // ***THE CASE THIS WHOLE VERSION EXISTS FOR.*** v2 wrote the barn as a bare
  // run of `kItemCount` counts with no length, so the size of the item
  // catalogue was part of the format's GEOMETRY. Appending `kEgg` therefore
  // made every v2 file on disk read one count too many — and the four bytes it
  // steals come out of the BUILDING COUNT, so the damage lands in a section the
  // barn code never touches and the error message would have blamed buildings.
  //
  // Append-only ids protect a stored index's MEANING. They say nothing about a
  // stored table's LENGTH.
  const std::vector<Byte> v2 = ReadV2Fixture();
  REQUIRE_FALSE(v2.empty());
  REQUIRE(static_cast<U8>(v2[4]) == 2);

  auto farm = DecodeSave(v2);
  if (!farm) {
    FAIL("the v2 fixture would not load: ", farm.error().message);
  }
  REQUIRE(farm.has_value());
  CHECK(farm->columns == 3);
  CHECK(farm->plots.size() == 9);

  // The barn came through with what it held, item for item.
  CHECK(farm->barn.capacity == 50);
  CHECK(farm->barn.Of(content::kWheatItem) == 6);
  CHECK(farm->barn.Of(content::kCornItem) == 3);
  CHECK(farm->barn.Of(content::kFlour) == 1);
  CHECK(farm->barn.Of(content::kMeal) == 0);
  // And the item that did not exist when it was written is simply zero, which
  // is the truth: the player never had any eggs.
  CHECK(farm->barn.Of(content::kEgg) == 0);

  // The section AFTER the barn is the one a length bug would wreck, so it is
  // checked explicitly rather than trusted.
  CHECK(farm->buildings.size() == 1);
  CHECK(farm->purse.coin == 42);
  CHECK(farm->land.owned == 5);

  // A farm that predates the coop gets one with no birds. app/ stocks it, for
  // the same reason it grants a new farm its mill.
  CHECK(farm->coop.animals == 0);
  CHECK(farm->coop.fed_until == 0);
}

TEST_CASE("the coop round-trips through v3") {
  runtime::SavedFarm farm;
  farm.saved_at = 1'700'000'000;
  farm.tick = 90'000;
  farm.rng_state = 0xABCDEF12u;
  farm.columns = 2;
  farm.plots.resize(4);
  farm.buildings.resize(1);
  farm.land.owned = 4;
  farm.coop =
      runtime::Coop{.animals = 4, .fed_until = 123'456, .next_lay = 91'000};
  (void)farm.barn.Add(content::kEgg, 7);

  auto back = DecodeSave(EncodeSave(farm));
  REQUIRE(back.has_value());
  CHECK(back->coop.animals == 4);
  CHECK(back->coop.fed_until == 123'456);
  CHECK(back->coop.next_lay == 91'000);
  CHECK(back->barn.Of(content::kEgg) == 7);
}

TEST_CASE("A SAVE FROM A LATER BUILD IS REFUSED, NOT TRUNCATED") {
  // Silently dropping items this build does not know would look to the player
  // like the barn had been emptied — and the next autosave would make that
  // permanent. The one case where refusing to load is the kind thing.
  runtime::SavedFarm farm;
  farm.columns = 1;
  farm.plots.resize(1);
  farm.land.owned = 1;
  std::vector<Byte> bytes = EncodeSave(farm);

  // Reach into the encoded payload and claim one more item than exists. The
  // count sits right after the barn capacity, which is right after the plot
  // table — computed rather than guessed so it survives a header change.
  constexpr Usize kBlobHeader = 4 + 1 + 1 + 2;  // magic, version, min, reserved
  const Usize offset = kBlobHeader + (8 + 8 + 8 + 4 + 4) + (1 + 2 + 8 + 8) + 4;
  REQUIRE(offset + 4 <= bytes.size());
  const auto bumped = static_cast<U32>(content::kItemCount + 1);
  for (Usize i = 0; i < 4; ++i) {
    bytes[offset + i] = static_cast<Byte>((bumped >> (i * 8)) & 0xFFu);
  }

  auto out = DecodeSave(bytes);
  REQUIRE_FALSE(out.has_value());
  CHECK(out.error().message.find("newer build") != std::string::npos);
}

// --- v4: buildings gained a kind and a place ---------------------------------

TEST_CASE("v4 ROUND-TRIPS a building's kind and cell") {
  SavedFarm farm;
  farm.columns = 3;
  farm.plots.resize(9);
  farm.buildings.resize(2);
  farm.buildings[0].kind = content::kMillKind;
  farm.buildings[0].cell = runtime::BuildCell{.col = 14, .row = 7};
  farm.buildings[0].queued = 1;
  farm.buildings[0].queue[0] = content::kGrindWheat;
  farm.buildings[1].kind = content::kMillKind;
  farm.buildings[1].cell = runtime::BuildCell{.col = 2, .row = 12};

  const auto decoded = DecodeSave(EncodeSave(farm));
  if (!decoded) {
    FAIL("a v4 farm would not load: ", decoded.error().message);
  }
  REQUIRE(decoded->buildings.size() == 2);
  CHECK(decoded->buildings[0].cell == farm.buildings[0].cell);
  CHECK(decoded->buildings[0].queued == 1);
  CHECK(decoded->buildings[1].cell == farm.buildings[1].cell);
  CHECK(decoded->buildings[1].kind == content::kMillKind);
}

TEST_CASE("A PRE-v4 FARM'S MILL LANDS WHERE THE AUTHORED ONE STOOD") {
  // The migration that matters. Before v4 a building had no position at all,
  // because the mill the player saw was a mesh in farm.world.json that nothing
  // connected to the building table. Defaulting its cell would put it in the
  // ring's CORNER — a legal cell, and visibly the wrong one.
  const std::vector<Byte> v2 = ReadV2Fixture();
  REQUIRE_FALSE(v2.empty());
  const auto farm = DecodeSave(v2);
  REQUIRE(farm.has_value());
  REQUIRE(farm->buildings.size() == 1);
  CHECK(farm->buildings[0].kind == content::kMillKind);
  CHECK(farm->buildings[0].cell == runtime::kStarterMillCell);

  SUBCASE("and the v1 fixture gets the same treatment") {
    const std::vector<Byte> v1 = ReadV1Fixture();
    REQUIRE_FALSE(v1.empty());
    const auto old = DecodeSave(v1);
    REQUIRE(old.has_value());
    REQUIRE(old->buildings.size() == 1);
    CHECK(old->buildings[0].cell == runtime::kStarterMillCell);
  }
}

TEST_CASE("a v4 file naming a kind that is not in the catalogue is REFUSED") {
  // Same class as the recipe and crop checks: a kind removed from the table
  // would index off the end of it on every draw and every queue.
  SavedFarm farm;
  farm.columns = 3;
  farm.plots.resize(9);
  farm.buildings.resize(1);
  farm.buildings[0].kind = 0xBEEF;
  const auto decoded = DecodeSave(EncodeSave(farm));
  REQUIRE_FALSE(decoded.has_value());
  CHECK(decoded.error().code == Errc::kParseError);
}

// --- v5: the archipelago -----------------------------------------------------

TEST_CASE("the v5 archipelago round-trips") {
  SavedFarm farm;
  farm.columns = 3;
  farm.plots.resize(9);
  farm.land.owned = 9;
  farm.isles.unlocked = (1u << content::kHub) | (1u << content::kFarIsle);
  farm.isles.current = content::kFarIsle;

  const std::vector<Byte> bytes = EncodeSave(farm);
  CHECK(static_cast<U8>(bytes[4]) == runtime::kSaveFormat.current);
  const auto back = DecodeSave(bytes);
  REQUIRE(back.has_value());
  CHECK(back->isles.unlocked == farm.isles.unlocked);
  CHECK(back->isles.current == content::kFarIsle);
}

TEST_CASE("A PRE-SKY-WORLD SAVE OWNS THE HUB AND STANDS ON IT") {
  // THE MIGRATION, and it is the whole of it: a farm written before the
  // archipelago existed was on the only island there was. Nothing is taken
  // away and nothing is asked — the same bargain v1 got when locked land
  // arrived and its whole board stayed its own.
  //
  // Checked against the REAL fixtures rather than a hand-built v4: both were
  // written by builds that had never heard of an island, and both take the
  // identical `version >= 5` branch — which is to say, neither takes it.
  SUBCASE("the v1 fixture") {
    const auto farm = DecodeSave(ReadV1Fixture());
    REQUIRE(farm.has_value());
    CHECK(farm->isles.unlocked == (1u << content::kHub));
    CHECK(farm->isles.current == content::kHub);
  }
  SUBCASE("the v2 fixture") {
    const auto farm = DecodeSave(ReadV2Fixture());
    REQUIRE(farm.has_value());
    CHECK(farm->isles.unlocked == (1u << content::kHub));
    CHECK(farm->isles.current == content::kHub);
  }
}

TEST_CASE("a file CLAIMING v5 but cut short of the archipelago is REFUSED") {
  // The blob carries no checksum, so a truncated payload reaches the reader
  // intact-looking and this branch is the only thing between it and eight
  // bytes of whatever follows. Dropping exactly the archipelago's 8 bytes
  // leaves a file whose HEADER still says v5.
  SavedFarm farm;
  farm.columns = 3;
  farm.plots.resize(9);
  farm.land.owned = 9;
  std::vector<Byte> bytes = EncodeSave(farm);
  REQUIRE(bytes.size() > 8);
  bytes.resize(bytes.size() - 8);
  const auto decoded = DecodeSave(bytes);
  REQUIRE_FALSE(decoded.has_value());
  CHECK(decoded.error().code == Errc::kParseError);
}

// --- v6: the dormant islands -------------------------------------------------

TEST_CASE("the v6 dormant islands round-trip") {
  SavedFarm farm;
  farm.columns = 3;
  farm.plots.resize(9);
  farm.land.owned = 9;
  farm.purse.coin = 42;
  farm.isles.current = content::kNearIsle;
  farm.isles.unlocked = (1u << content::kHub) | (1u << content::kNearIsle);

  SavedFarm hub;
  hub.columns = 2;
  hub.plots.resize(4);
  hub.land.owned = 3;
  hub.barn.Add(content::kWheatItem, 7);
  hub.saved_at = 1234;
  farm.dormant_ids.push_back(content::kHub);
  farm.dormant.push_back(hub);

  const std::vector<Byte> bytes = EncodeSave(farm);
  CHECK(static_cast<U8>(bytes[4]) == runtime::kSaveFormat.current);
  const auto back = DecodeSave(bytes);
  REQUIRE(back.has_value());

  // The live island is unchanged...
  CHECK(back->land.owned == 9);
  CHECK(back->purse.coin == 42);
  CHECK(back->isles.current == content::kNearIsle);
  // ...and the dormant one came back whole, including the stamp its next
  // catch-up depends on.
  REQUIRE(back->dormant.size() == 1);
  CHECK(back->dormant_ids[0] == content::kHub);
  CHECK(back->dormant[0].columns == 2);
  CHECK(back->dormant[0].land.owned == 3);
  CHECK(back->dormant[0].barn.Of(content::kWheatItem) == 7);
  CHECK(back->dormant[0].saved_at == 1234);
  // ONE LEVEL ONLY: a dormant record never carries dormant records of its own.
  CHECK(back->dormant[0].dormant.empty());
}

TEST_CASE("A PRE-v6 SAVE HAS NO DORMANT ISLANDS, WHICH IS THE MIGRATION") {
  // The farm a v1/v2 file holds IS the island it says it is standing on, and
  // there were never any others. No branch is taken and nothing is asked —
  // the same bargain every migration here has made since v2.
  SUBCASE("the v1 fixture") {
    const auto farm = DecodeSave(ReadV1Fixture());
    REQUIRE(farm.has_value());
    CHECK(farm->dormant.empty());
    CHECK(farm->dormant_ids.empty());
    // and its own farm survived intact, as the v1 migration test asserts
    CHECK(farm->land.owned == 9);
  }
  SUBCASE("the v2 fixture") {
    const auto farm = DecodeSave(ReadV2Fixture());
    REQUIRE(farm.has_value());
    CHECK(farm->dormant.empty());
  }
}

TEST_CASE("a v6 file cut short of its dormant islands is REFUSED") {
  SavedFarm farm;
  farm.columns = 3;
  farm.plots.resize(9);
  farm.land.owned = 9;
  SavedFarm other;
  other.columns = 2;
  other.plots.resize(4);
  farm.dormant_ids.push_back(content::kNearIsle);
  farm.dormant.push_back(other);

  std::vector<Byte> bytes = EncodeSave(farm);
  // Lop the dormant record off but leave the COUNT saying there is one. The
  // header still says v6, so the reader walks into a record that is not there.
  bytes.resize(bytes.size() - 20);
  const auto decoded = DecodeSave(bytes);
  REQUIRE_FALSE(decoded.has_value());
  CHECK(decoded.error().code == Errc::kParseError);
}

TEST_CASE("A v6 FILE CLAIMING MORE ISLANDS THAN A MASK HOLDS IS REFUSED") {
  // WHAT THIS DOES AND DOES NOT PROVE. It asserts the refusal, which is the
  // behaviour that matters. It does NOT pin the 32-island bound: mutation
  // testing showed this test passes with that bound removed, because an
  // inflated count then walks into the first truncated record and is refused
  // there instead. Both paths are correct; only the message differs. Recorded
  // rather than quietly left as a test that looks stronger than it is.
  SavedFarm farm;
  farm.columns = 3;
  farm.plots.resize(9);
  farm.land.owned = 9;
  std::vector<Byte> bytes = EncodeSave(farm);
  // The count is the last four bytes of a save with no dormant islands.
  REQUIRE(bytes.size() > 4);
  const Usize at = bytes.size() - 4;
  bytes[at] = static_cast<Byte>(0xFF);
  bytes[at + 1] = static_cast<Byte>(0xFF);
  const auto decoded = DecodeSave(bytes);
  REQUIRE_FALSE(decoded.has_value());
  CHECK(decoded.error().code == Errc::kParseError);
}

TEST_CASE("A DORMANT RECORD FOR AN ISLAND THIS BUILD LACKS IS DROPPED") {
  // Same not-trusted-as-written rule IslandsFromSave follows: everything
  // downstream indexes content::kIslands by this id, so a record naming one
  // past the end is dropped rather than kept. A file from a later build loses
  // those farms, which is honest — this build cannot show them anywhere.
  SavedFarm farm;
  farm.columns = 3;
  farm.plots.resize(9);
  farm.land.owned = 9;
  SavedFarm ghost;
  ghost.columns = 2;
  ghost.plots.resize(4);
  farm.dormant_ids.push_back(
      static_cast<content::IslandId>(content::kIslands.size()));
  farm.dormant.push_back(ghost);
  SavedFarm real;
  real.columns = 2;
  real.plots.resize(4);
  real.land.owned = 1;
  farm.dormant_ids.push_back(content::kNearIsle);
  farm.dormant.push_back(real);

  const auto back = DecodeSave(EncodeSave(farm));
  REQUIRE(back.has_value());
  REQUIRE(back->dormant.size() == 1);
  CHECK(back->dormant_ids[0] == content::kNearIsle);
  CHECK(back->dormant[0].land.owned == 1);
}
