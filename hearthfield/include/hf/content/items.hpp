// What the barn holds and what an order asks for.
//
// A SEPARATE id space from crops, and it has to be: an order asks for flour and
// no crop grows flour. A crop is a thing you plant; an item is a thing you own.
// They overlap (wheat is both) and they are not the same list.
#pragma once

#include <array>
#include <string_view>

#include "aether/core/types.hpp"

namespace hearthfield::content {

using ItemId = aether::U16;

struct Item {
  std::string_view name;
};

// ITEM IDS ARE APPEND-ONLY, for the same reason crop ids are: a save stores the
// index, so removing or reordering turns every barn on disk into a barn full of
// something else. Add at the end.
inline constexpr ItemId kWheatItem = 0;
inline constexpr ItemId kCornItem = 1;
inline constexpr ItemId kFlour = 2;
inline constexpr ItemId kMeal = 3;
// H5. Appending this was correct AND not sufficient: the save's v2 payload had
// no item COUNT in it, so `kItemCount` was part of the format's geometry and
// this one line would have misparsed every farm on disk (H5 plan §3c).
// Append-only ids protect a stored index's MEANING; they say nothing about a
// stored table's LENGTH. Save v3 writes the length.
inline constexpr ItemId kEgg = 4;

inline constexpr std::array<Item, 5> kItems = {{
    Item{.name = "wheat"},
    Item{.name = "corn"},
    Item{.name = "flour"},
    Item{.name = "meal"},
    Item{.name = "egg"},
}};

inline constexpr aether::Usize kItemCount = kItems.size();

[[nodiscard]] constexpr bool ItemExists(ItemId id) { return id < kItemCount; }

[[nodiscard]] constexpr const Item& ItemById(ItemId id) {
  return kItems[static_cast<aether::Usize>(id)];
}

constexpr bool AllItemsNamed() {
  for (const Item& item : kItems) {
    if (item.name.empty()) {
      return false;
    }
  }
  return true;
}
static_assert(AllItemsNamed(), "an item has no name");

}  // namespace hearthfield::content
