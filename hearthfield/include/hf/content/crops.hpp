// The crop catalogue — declarative data, validated at compile time.
//
// Durations are SECONDS, never ticks. The fixed step is a config.json key
// (`fixed_dt`), so a catalogue written in ticks would silently rescale the
// whole economy when that key moved — and a save carries tick counts, so the
// damage would reach files already on disk. runtime::TicksFromSeconds is the
// one site that knows the rate.
#pragma once

#include <array>
#include <string_view>

#include "aether/core/types.hpp"
#include "hf/content/items.hpp"

namespace hearthfield::content {

using CropId = aether::U16;

struct Crop {
  std::string_view name;
  aether::F64 grow_seconds;
  aether::U32 yield;  // units added to the barn per harvest
  ItemId yields = 0;  // WHICH item — a crop and its produce are separate ids
};

// CROP IDS ARE APPEND-ONLY, and this is a save-format rule rather than a
// stylistic one. They are indices into the table below, and a save stores the
// index — so removing or reordering a crop silently turns every planted wheat
// into something else in every farm already on disk. Add at the end; to retire
// a crop, leave the row and stop offering it.
inline constexpr CropId kWheat = 0;
inline constexpr CropId kCorn = 1;

// The timers ARE the level design (spec §1), so this table is the game's tuning
// and not merely its content. Ten minutes for wheat is the spec's own figure;
// corn is slower and worth more, which is the first real choice the board
// offers — plant the thing that will be ready when you are back.
inline constexpr std::array<Crop, 2> kCrops = {{
    Crop{.name = "wheat",
         .grow_seconds = 600.0,
         .yield = 2,
         .yields = kWheatItem},
    Crop{.name = "corn",
         .grow_seconds = 1500.0,
         .yield = 5,
         .yields = kCornItem},
}};

[[nodiscard]] constexpr bool CropExists(CropId id) {
  return id < kCrops.size();
}

[[nodiscard]] constexpr const Crop& CropById(CropId id) {
  return kCrops[static_cast<aether::Usize>(id)];
}

// The validation pass the content convention asks for, at compile time because
// this table is constexpr. A zero-duration crop would be ready the tick it was
// sown; a zero yield would make harvesting pointless — both are authoring
// slips rather than designs, and neither should reach a test.
constexpr bool AllCropsWellFormed() {
  for (const Crop& crop : kCrops) {
    if (crop.grow_seconds <= 0.0 || crop.yield == 0 || crop.name.empty() ||
        !ItemExists(crop.yields)) {
      return false;
    }
  }
  return true;
}
static_assert(AllCropsWellFormed(), "a crop has no name, no time or no yield");

}  // namespace hearthfield::content
