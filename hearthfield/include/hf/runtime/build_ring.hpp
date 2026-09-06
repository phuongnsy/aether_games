// Where a building may stand: the lattice around the field, and what is free.
//
// Pure geometry over the building table, in `runtime` for the same three
// callers `grid.hpp` names — view/ places the meshes, app/ resolves the
// pointer, and the tests check the arithmetic with no device. It names no
// scene, no renderer and no viewport.
//
// THE TWO LATTICES COINCIDE BY CONSTRUCTION, and that is the reason for the
// numbers rather than a coincidence to preserve by hand. Plots put their
// centres at `(i - (columns-1)/2) * cell` over 24 columns, so at -11.5..11.5.
// The ring uses the same expression over 32, so at -15.5..15.5 — and its
// indices 4..27 land on exactly -11.5..11.5, the plot centres. So "is this ring
// cell part of the field" is an index comparison and never a float one.
//
// THE NUMBERS IN THIS PARAGRAPH ARE THE ONLY ONES HERE THAT ARE NOT DERIVED, so
// they are the ones that rot: they read 8, 16 and 4..11 until the board grew on
// 2026-08-28, and nothing but reading them says otherwise.
#pragma once

#include <optional>
#include <span>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "hf/content/buildings.hpp"
#include "hf/content/farm.hpp"
#include "hf/runtime/chain.hpp"

namespace hearthfield::runtime {

// `BuildCell` itself lives in chain.hpp, beside the building that stands on it
// — this header reads the building table, so the dependency runs that way.

// THIRTY-TWO columns of the same cell as the field, centred on it: the board's
// 24 plus a 4-cell margin each side, which is the margin 16-around-8 had. Wide
// enough for the authored steading (the mill stands at x = 14.0, cells 29-30);
// small enough that occupancy is a loop nobody has to think about.
//
// THIS MUST GROW WITH THE BOARD, AND IT IS NOT AUTOMATIC. `kFieldFirst` below
// is U32, so a board WIDER than its ring underflows rather than failing to
// compile: raising the board 8 -> 24 turned three placement tests red with
// nothing pointing at the ring as the cause (event:2026-08-28#24).
inline constexpr aether::U32 kRingColumns = 32;
// The field's first ring index. (32 - 24) / 2 = 4, written as the derivation so
// changing either column count keeps the two lattices concentric.
inline constexpr aether::U32 kFieldFirst =
    (kRingColumns - content::kDefaultColumns) / 2;
inline constexpr aether::U32 kFieldLast =
    kFieldFirst + content::kDefaultColumns - 1;

// Where the starter mill stands, as a 2x2 whose span centre is (14.0, 0).
//
// THE LATTICE IS THE SOURCE OF TRUTH AND THE WORLD FILE FOLLOWS IT. Both props
// were authored at ±6.4, which is 0.4 m off any cell-span centre — near enough
// to look right and far enough that a footprint reserved from a rounded cell
// would not sit under the mesh. Moving the authored positions to ±6.0 makes the
// two exact, and `hearthfield_tests` asserts it: a content constant and an
// authored position that must match are exactly the pair that drifts.
//
// A 2-span's centre in a 32-column ring is `col - 15.0`, so 29 is +14.0 and 1
// is -14.0. Every steading position moved outward by exactly +8 m when the
// board grew 8 -> 24, which preserves each one's CLEARANCE from the field edge
// rather than merely getting it off the plots.
inline constexpr BuildCell kStarterMillCell{.col = 29, .row = 15};

// The authored coop, reserved so a mill cannot be built on top of it. Content
// rather than read from the chunk: the sim has no `resources` and must not
// gain one — placement has to be decidable from LatchedInput alone or the
// replay oracle stops meaning anything (the plan's D3).
inline constexpr BuildCell kCoopCell{.col = 1, .row = 15};
inline constexpr aether::U32 kCoopSpan = 2;

struct BuildRing {
  aether::U32 columns = kRingColumns;
  aether::F32 cell = content::kCellSize;

  [[nodiscard]] constexpr bool Has(BuildCell at) const {
    return at.col < columns && at.row < columns;
  }

  // The centre of one cell on the ground.
  [[nodiscard]] constexpr aether::Vec3 CenterOf(BuildCell at) const {
    const aether::F32 half = 0.5f * static_cast<aether::F32>(columns - 1);
    return aether::Vec3{(static_cast<aether::F32>(at.col) - half) * cell, 0.0f,
                        (static_cast<aether::F32>(at.row) - half) * cell};
  }

  // The centre of a SPAN of cells, which is where its mesh goes. A 2x2 sits on
  // the corner between its four cells, not on any one of them.
  [[nodiscard]] constexpr aether::Vec3 CenterOfSpan(BuildCell at,
                                                    aether::U32 span) const {
    const aether::Vec3 first = CenterOf(at);
    const aether::F32 offset = 0.5f * static_cast<aether::F32>(span - 1) * cell;
    return aether::Vec3{first.x + offset, 0.0f, first.z + offset};
  }

  // The cell containing a point on the ground, or nothing if it is off the
  // ring. Floor, not round: a cell owns [centre - half, centre + half).
  [[nodiscard]] constexpr std::optional<BuildCell> CellAt(
      aether::Vec3 ground) const {
    const aether::F32 half = 0.5f * static_cast<aether::F32>(columns) * cell;
    const aether::F32 col = (ground.x + half) / cell;
    const aether::F32 row = (ground.z + half) / cell;
    if (col < 0.0f || row < 0.0f || col >= static_cast<aether::F32>(columns) ||
        row >= static_cast<aether::F32>(columns)) {
      return std::nullopt;
    }
    return BuildCell{.col = static_cast<aether::U16>(col),
                     .row = static_cast<aether::U16>(row)};
  }

  // Whether this cell is part of the crop field. The field is the ring's
  // central square, so this is the index test the header note describes.
  [[nodiscard]] constexpr bool IsField(BuildCell at) const {
    return at.col >= kFieldFirst && at.col <= kFieldLast &&
           at.row >= kFieldFirst && at.row <= kFieldLast;
  }
};

// Whether two square footprints share any cell.
[[nodiscard]] constexpr bool SpansOverlap(BuildCell a, aether::U32 a_span,
                                          BuildCell b, aether::U32 b_span) {
  return a.col < b.col + b_span && b.col < a.col + a_span &&
         a.row < b.row + b_span && b.row < a.row + a_span;
}

// Why a placement was refused. Reported rather than collapsed to a bool: the
// player is told which rule they hit, and a test can tell "off the edge" from
// "on the coop" without reading pixels.
enum class PlaceRefusal : aether::U8 {
  kOk,
  kOffRing,   // the footprint leaves the buildable area
  kOnField,   // it covers a crop plot
  kOccupied,  // another building, or the authored coop, is there
  kUnknownKind,
};

// The whole rule, in one pure function. Every caller asks THIS — the feature to
// commit, the view to tint the ghost — so the answer a player sees and the
// answer the sim enforces cannot disagree.
[[nodiscard]] inline PlaceRefusal CanPlace(const BuildRing& ring,
                                           content::BuildingKind kind,
                                           BuildCell at,
                                           std::span<const Building> existing) {
  if (!content::BuildingKindExists(kind)) {
    return PlaceRefusal::kUnknownKind;
  }
  const aether::U32 span = content::BuildingTypeOf(kind).span;
  // The far corner must be on the ring too, so a footprint cannot overhang.
  if (!ring.Has(at) ||
      !ring.Has(
          BuildCell{.col = static_cast<aether::U16>(at.col + span - 1),
                    .row = static_cast<aether::U16>(at.row + span - 1)})) {
    return PlaceRefusal::kOffRing;
  }
  for (aether::U32 dc = 0; dc < span; ++dc) {
    for (aether::U32 dr = 0; dr < span; ++dr) {
      if (ring.IsField(
              BuildCell{.col = static_cast<aether::U16>(at.col + dc),
                        .row = static_cast<aether::U16>(at.row + dr)})) {
        return PlaceRefusal::kOnField;
      }
    }
  }
  if (SpansOverlap(at, span, kCoopCell, kCoopSpan)) {
    return PlaceRefusal::kOccupied;
  }
  for (const Building& building : existing) {
    if (SpansOverlap(at, span, building.cell,
                     content::BuildingTypeOf(building.kind).span)) {
      return PlaceRefusal::kOccupied;
    }
  }
  return PlaceRefusal::kOk;
}

}  // namespace hearthfield::runtime
