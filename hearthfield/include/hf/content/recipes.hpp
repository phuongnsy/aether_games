// What a building turns one thing into another with.
//
// The spec's §1 figure is the design: "wheat is ten minutes and the mill is
// twenty, so a wheat field feeds a mill that is always slightly hungry". These
// durations ARE that sentence, and they are in seconds for the same reason crop
// times are — the fixed step is a config key (ADR-0106).
#pragma once

#include <array>
#include <string_view>

#include "aether/core/types.hpp"
#include "hf/content/items.hpp"

namespace hearthfield::content {

using RecipeId = aether::U16;

struct Recipe {
  std::string_view name;
  ItemId input = 0;
  aether::U32 input_count = 0;
  ItemId output = 0;
  aether::U32 output_count = 0;
  aether::F64 seconds = 0.0;
};

// APPEND-ONLY. A save stores the recipe index inside a building's queue, so a
// mill mid-grind would start producing something else.
inline constexpr RecipeId kGrindWheat = 0;
inline constexpr RecipeId kGrindCorn = 1;

inline constexpr std::array<Recipe, 2> kRecipes = {{
    Recipe{.name = "flour",
           .input = kWheatItem,
           .input_count = 2,
           .output = kFlour,
           .output_count = 1,
           .seconds = 1200.0},  // twenty minutes: the spec's own number
    Recipe{.name = "meal",
           .input = kCornItem,
           .input_count = 2,
           .output = kMeal,
           .output_count = 1,
           .seconds = 1800.0},
}};

[[nodiscard]] constexpr bool RecipeExists(RecipeId id) {
  return id < kRecipes.size();
}

[[nodiscard]] constexpr const Recipe& RecipeById(RecipeId id) {
  return kRecipes[static_cast<aether::Usize>(id)];
}

constexpr bool AllRecipesWellFormed() {
  for (const Recipe& recipe : kRecipes) {
    if (recipe.seconds <= 0.0 || recipe.input_count == 0 ||
        recipe.output_count == 0 || !ItemExists(recipe.input) ||
        !ItemExists(recipe.output) || recipe.input == recipe.output) {
      return false;
    }
  }
  return true;
}
// `input == output` is refused too: a recipe that consumes and produces the
// same item is either a typo or an infinite-money machine, and both want
// catching at compile time rather than in a player's economy.
static_assert(AllRecipesWellFormed(), "a recipe is malformed");

}  // namespace hearthfield::content
