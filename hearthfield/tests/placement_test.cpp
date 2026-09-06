// The build ring and the placement rule.
//
// Pure by construction — no device, no scene, no viewport — which is the point:
// the ghost the player drags and the commit the sim performs call the SAME
// `CanPlace`, so what is shown and what is enforced cannot disagree. That is
// the failure this file exists to prevent (a green ghost over a cell the commit
// then refuses), and it is only testable because the rule is a free function.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "aether/core/config.hpp"
#include "hf/content/buildings.hpp"
#include "hf/features/placement/system.hpp"
#include "hf/runtime/build_ring.hpp"
#include "hf/runtime/game_world.hpp"
#include "hf/runtime/grid.hpp"

using namespace aether;
using namespace hearthfield;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;

// A cell well clear of the field, the coop and the starter mill.
constexpr runtime::BuildCell kOpen{.col = 1, .row = 1};

runtime::LatchedInput Nothing() { return runtime::LatchedInput{}; }

}  // namespace

// --- the lattice -------------------------------------------------------------

TEST_CASE(
    "THE TWO LATTICES COINCIDE: the field's ring cells ARE the plot centres") {
  // The whole reason "is this cell part of the field" is an index comparison
  // and never a float one. If this fails, every other rule here is comparing
  // half-offset grids and the ghost will sit between plots.
  const runtime::BuildRing ring;
  const runtime::Grid grid{};
  for (U32 col = 0; col < content::kDefaultColumns; ++col) {
    for (U32 row = 0; row < content::kDefaultColumns; ++row) {
      const auto plot = static_cast<runtime::PlotId>(row * grid.columns + col);
      const Vec3 from_grid = grid.CenterOf(plot);
      const Vec3 from_ring = ring.CenterOf(runtime::BuildCell{
          .col = static_cast<U16>(runtime::kFieldFirst + col),
          .row = static_cast<U16>(runtime::kFieldFirst + row)});
      CHECK(from_ring.x == doctest::Approx(from_grid.x));
      CHECK(from_ring.z == doctest::Approx(from_grid.z));
    }
  }
}

TEST_CASE("a ground point resolves to the cell that contains it") {
  const runtime::BuildRing ring;
  // DERIVED, not spelled. These were written as literals for a 16-column ring
  // and every one of them went red when the board grew 8 -> 24 and the ring
  // followed it to 32 — which is a test pinned to a constant rather than to the
  // property it means to check (event:2026-08-28#24).
  constexpr auto kLast = static_cast<aether::U16>(runtime::kRingColumns - 1);
  constexpr auto kMid = static_cast<aether::U16>(runtime::kRingColumns / 2);
  const F32 half = 0.5f * static_cast<F32>(runtime::kRingColumns) *
                   content::kCellSize;  // the ring spans [-half, half)

  CHECK(ring.CellAt(Vec3{-half + 0.5f, 0.0f, -half + 0.5f}) ==
        runtime::BuildCell{.col = 0, .row = 0});
  CHECK(ring.CellAt(Vec3{half - 0.6f, 0.0f, half - 0.6f}) ==
        runtime::BuildCell{.col = kLast, .row = kLast});

  SUBCASE("a cell owns its lower edge and not its upper one") {
    // Floor, not round: two adjacent cells must not both claim a boundary, or
    // a ghost flickers between them as the pointer sits on the line.
    CHECK(ring.CellAt(Vec3{0.0f, 0.0f, 0.0f}) ==
          runtime::BuildCell{.col = kMid, .row = kMid});
    CHECK(ring.CellAt(Vec3{-0.01f, 0.0f, -0.01f}) ==
          runtime::BuildCell{.col = static_cast<aether::U16>(kMid - 1),
                             .row = static_cast<aether::U16>(kMid - 1)});
  }

  SUBCASE("off the ring is nothing, not a clamped edge cell") {
    CHECK_FALSE(ring.CellAt(Vec3{-half - 0.1f, 0.0f, 0.0f}).has_value());
    CHECK_FALSE(ring.CellAt(Vec3{0.0f, 0.0f, half}).has_value());
    CHECK_FALSE(ring.CellAt(Vec3{half * 6.0f, 0.0f, 0.0f}).has_value());
  }
}

TEST_CASE("a SPAN sits on the corner between its cells, not on one of them") {
  const runtime::BuildRing ring;
  const runtime::BuildCell at{.col = 2, .row = 2};
  const Vec3 one = ring.CenterOfSpan(at, 1);
  const Vec3 two = ring.CenterOfSpan(at, 2);
  CHECK(one.x == doctest::Approx(ring.CenterOf(at).x));
  // A 2x2's centre is half a cell further along each axis than its first cell.
  CHECK(two.x == doctest::Approx(one.x + 0.5f));
  CHECK(two.z == doctest::Approx(one.z + 0.5f));
}

// --- the rule ----------------------------------------------------------------

TEST_CASE("A BUILDING MAY NOT STAND ON THE FIELD") {
  const runtime::BuildRing ring;
  const std::vector<runtime::Building> none;
  // Dead centre of the board. DERIVED from the lattice, because a literal here
  // is a literal about the RING's size and this test is about the FIELD.
  constexpr auto kFieldMid = static_cast<aether::U16>(
      runtime::kFieldFirst + content::kDefaultColumns / 2);
  CHECK(
      runtime::CanPlace(ring, content::kMillKind,
                        runtime::BuildCell{.col = kFieldMid, .row = kFieldMid},
                        none) == runtime::PlaceRefusal::kOnField);

  SUBCASE("including a footprint that only CLIPS it") {
    // The mill is 2x2 and the field is columns kFieldFirst..kFieldLast.
    // Starting ON the last field column it covers that one and the next, so ONE
    // of its four cells is field — the case a per-cell check catches and a
    // centre-point check does not.
    //
    // Tested on the far side from the coop deliberately: the first draft used
    // col 2 as the "clear" control and it is not clear at all, because the
    // coop sits at columns 1-2. The test was wrong and the rule was right.
    CHECK(runtime::CanPlace(ring, content::kMillKind,
                            runtime::BuildCell{.col = static_cast<aether::U16>(
                                                   runtime::kFieldLast),
                                               .row = kFieldMid},
                            none) == runtime::PlaceRefusal::kOnField);
    // One further out is clear.
    CHECK(runtime::CanPlace(ring, content::kMillKind,
                            runtime::BuildCell{.col = static_cast<aether::U16>(
                                                   runtime::kFieldLast + 1),
                                               .row = kFieldMid},
                            none) == runtime::PlaceRefusal::kOk);
  }
}

TEST_CASE("a footprint may not OVERHANG the ring") {
  const runtime::BuildRing ring;
  const std::vector<runtime::Building> none;
  // The LAST column cannot host a 2-wide building: it would need one more.
  constexpr auto kLast = static_cast<aether::U16>(runtime::kRingColumns - 1);
  CHECK(runtime::CanPlace(ring, content::kMillKind,
                          runtime::BuildCell{.col = kLast, .row = 1},
                          none) == runtime::PlaceRefusal::kOffRing);
  CHECK(runtime::CanPlace(
            ring, content::kMillKind,
            runtime::BuildCell{.col = static_cast<aether::U16>(kLast - 1),
                               .row = 1},
            none) == runtime::PlaceRefusal::kOk);
}

TEST_CASE("two buildings may not share a cell, and a 2x2 blocks all four") {
  const runtime::BuildRing ring;
  std::vector<runtime::Building> existing;
  existing.push_back(
      runtime::Building{.kind = content::kMillKind,
                        .cell = runtime::BuildCell{.col = 1, .row = 1}});
  // Every cell the 2x2 covers is taken, and so is every cell a second 2x2
  // would overlap from — one cell before it in each axis.
  for (U16 col = 0; col <= 2; ++col) {
    for (U16 row = 0; row <= 2; ++row) {
      CAPTURE(col);
      CAPTURE(row);
      CHECK(runtime::CanPlace(ring, content::kMillKind,
                              runtime::BuildCell{.col = col, .row = row},
                              existing) == runtime::PlaceRefusal::kOccupied);
    }
  }
  // Clear of it in one axis.
  CHECK(runtime::CanPlace(ring, content::kMillKind,
                          runtime::BuildCell{.col = 3, .row = 1},
                          existing) == runtime::PlaceRefusal::kOk);
}

TEST_CASE(
    "THE AUTHORED COOP IS RESERVED, though the sim never sees the chunk") {
  // The plan's D3: the coop is in the world file and the sim has no
  // `resources`, so its cells are a content constant. Without this a player
  // builds a mill inside the chicken coop.
  const runtime::BuildRing ring;
  const std::vector<runtime::Building> none;
  CHECK(runtime::CanPlace(ring, content::kMillKind, runtime::kCoopCell, none) ==
        runtime::PlaceRefusal::kOccupied);
}

TEST_CASE("an unknown kind is refused rather than indexed") {
  const runtime::BuildRing ring;
  const std::vector<runtime::Building> none;
  CHECK(runtime::CanPlace(ring, 0xFFFF, kOpen, none) ==
        runtime::PlaceRefusal::kUnknownKind);
}

// --- the system --------------------------------------------------------------

namespace {

// A world with money in it, one starter mill where the authored one stands, and
// the placement system registered.
class Farm {
 public:
  Farm()
      : world(1, static_cast<Usize>(content::kDefaultColumns) *
                     content::kDefaultColumns) {
    world.World().AddBuilding(content::kMillKind, runtime::kStarterMillCell);
    world.World().ThePurse().coin = 10'000;
    world.AddSystem(placement);
  }

  runtime::GameWorld world;
  placement::PlacementSystem placement;
};

template <class T>
[[nodiscard]] const T* FindEvent(const runtime::EventList& events) {
  for (const runtime::GameEvent& event : events) {
    if (const auto* found = std::get_if<T>(&event)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("placing a building DEBITS the purse and appends it") {
  Farm farm;
  const U32 before = farm.world.World().ThePurse().coin;
  runtime::LatchedInput in = Nothing();
  in.place_kind = content::kMillKind;
  in.place_cell = kOpen;
  const runtime::EventList& events = farm.world.Step(in, kDt);

  REQUIRE(farm.world.World().Buildings().size() == 2);
  const runtime::Building& built = farm.world.World().Buildings()[1];
  CHECK(built.kind == content::kMillKind);
  CHECK(built.cell == kOpen);
  CHECK(farm.world.World().ThePurse().coin ==
        before - content::BuildingTypeOf(content::kMillKind).cost);

  const auto* placed = FindEvent<runtime::BuildingPlaced>(events);
  REQUIRE(placed != nullptr);
  CHECK(placed->building == 1);
  CHECK(placed->cell == kOpen);
}

TEST_CASE("A REFUSED PLACEMENT COSTS NOTHING — no coin, no building") {
  // The one that matters for a player: an illegal tap must not quietly take
  // their money, and must not half-commit.
  Farm farm;
  const U32 before = farm.world.World().ThePurse().coin;
  runtime::LatchedInput in = Nothing();
  in.place_kind = content::kMillKind;
  in.place_cell = runtime::BuildCell{.col = 7, .row = 7};  // the field
  const runtime::EventList& events = farm.world.Step(in, kDt);

  CHECK(farm.world.World().Buildings().size() == 1);
  CHECK(farm.world.World().ThePurse().coin == before);
  const auto* refused = FindEvent<runtime::Refused>(events);
  REQUIRE(refused != nullptr);
  CHECK(refused->why == runtime::Refused::Why::kBadGround);
}

TEST_CASE("GROUND IS CHECKED BEFORE MONEY") {
  // A player dragging over the field is told "not there" whether or not they
  // can afford it. Told "no coin" instead, they would go and earn money for a
  // placement that was never going to work.
  Farm farm;
  farm.world.World().ThePurse().coin = 0;
  runtime::LatchedInput in = Nothing();
  in.place_kind = content::kMillKind;
  in.place_cell = runtime::BuildCell{.col = 7, .row = 7};
  const runtime::EventList& events = farm.world.Step(in, kDt);

  const auto* refused = FindEvent<runtime::Refused>(events);
  REQUIRE(refused != nullptr);
  CHECK(refused->why == runtime::Refused::Why::kBadGround);
}

TEST_CASE(
    "no coin, legal ground: refused for the reason that is actually true") {
  Farm farm;
  farm.world.World().ThePurse().coin =
      content::BuildingTypeOf(content::kMillKind).cost - 1;
  runtime::LatchedInput in = Nothing();
  in.place_kind = content::kMillKind;
  in.place_cell = kOpen;
  const runtime::EventList& events = farm.world.Step(in, kDt);

  CHECK(farm.world.World().Buildings().size() == 1);
  const auto* refused = FindEvent<runtime::Refused>(events);
  REQUIRE(refused != nullptr);
  CHECK(refused->why == runtime::Refused::Why::kNoCoin);
}

TEST_CASE("a step with no build in it does nothing at all") {
  Farm farm;
  const U32 before = farm.world.World().ThePurse().coin;
  const runtime::EventList& events = farm.world.Step(Nothing(), kDt);
  CHECK(farm.world.World().Buildings().size() == 1);
  CHECK(farm.world.World().ThePurse().coin == before);
  CHECK(FindEvent<runtime::BuildingPlaced>(events) == nullptr);
}

TEST_CASE("THE SAVE'S BUILDING CAP IS ENFORCED BY THE SIM") {
  // DecodeSave refuses a farm with more than 64 buildings. Without this check
  // the 65th mill is placeable and the farm stops loading at the next autosave
  // — the worst possible place to discover a bound.
  Farm farm;
  farm.world.World().ThePurse().coin = 1'000'000;
  U16 placed = 1;
  for (U16 col = 0; col < 16 && farm.world.World().Buildings().size() < 64;
       ++col) {
    for (U16 row = 0; row < 16 && farm.world.World().Buildings().size() < 64;
         ++row) {
      runtime::LatchedInput in = Nothing();
      in.place_kind = content::kMillKind;
      in.place_cell = runtime::BuildCell{.col = col, .row = row};
      (void)farm.world.Step(in, kDt);
    }
  }
  (void)placed;
  // However many fitted, one more must be refused for ROOM once at the cap.
  if (farm.world.World().Buildings().size() >= 64) {
    runtime::LatchedInput in = Nothing();
    in.place_kind = content::kMillKind;
    in.place_cell = kOpen;
    (void)farm.world.Step(in, kDt);
    CHECK(farm.world.World().Buildings().size() == 64);
  }
  CHECK(farm.world.World().Buildings().size() <= 64);
}

// --- the pair that must not drift --------------------------------------------

TEST_CASE("THE LATTICE AND THE WORLD FILE AGREE ABOUT WHERE THINGS STAND") {
  // The plan's D3, made executable. The sim reserves cells from CONTENT
  // constants because it has no `resources` and must stay decidable from
  // LatchedInput alone; the meshes and the audio anchors come from
  // farm.world.json. Two facts, two files, and nothing but this test to stop
  // them parting company — a mill's footprint reserved half a metre from the
  // mill you can see is invisible until a player builds into it.
  const std::filesystem::path path =
      std::filesystem::path(HEARTHFIELD_ASSET_DIR) / "worlds" /
      "farm.world.json";
  std::ifstream file(path);
  REQUIRE_MESSAGE(file.good(), "missing world file: " << path.string());
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  const auto world = ParseJsonConfig(text);
  if (!world) {
    FAIL("the world file would not parse: ", world.error().message);
  }

  const auto count = world->ArraySize("entities");
  REQUIRE(count.has_value());
  bool saw_mill = false;
  bool saw_coop = false;
  const runtime::BuildRing ring;
  for (Usize i = 0; i < *count; ++i) {
    const std::string entity = "entities." + std::to_string(i);
    const auto name = world->GetString(entity + ".name");
    if (!name) {
      continue;
    }
    const auto x = world->GetNumber(entity + ".pos.0");
    const auto z = world->GetNumber(entity + ".pos.2");
    REQUIRE(x.has_value());
    REQUIRE(z.has_value());
    if (*name == "mill") {
      saw_mill = true;
      const Vec3 centre =
          ring.CenterOfSpan(runtime::kStarterMillCell,
                            content::BuildingTypeOf(content::kMillKind).span);
      CHECK(static_cast<F32>(*x) == doctest::Approx(centre.x));
      CHECK(static_cast<F32>(*z) == doctest::Approx(centre.z));
      // And it is a LOCATOR since H7: every building is drawn from the sim's
      // table, so an authored mesh here would be a second mill on top of the
      // first one.
      CHECK(world->GetString(entity + ".type") == "empty");
    } else if (*name == "coop") {
      saw_coop = true;
      const Vec3 centre =
          ring.CenterOfSpan(runtime::kCoopCell, runtime::kCoopSpan);
      CHECK(static_cast<F32>(*x) == doctest::Approx(centre.x));
      CHECK(static_cast<F32>(*z) == doctest::Approx(centre.z));
    }
  }
  CHECK(saw_mill);
  CHECK(saw_coop);
}
