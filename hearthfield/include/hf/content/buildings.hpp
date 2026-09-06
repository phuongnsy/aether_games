// What a player can build, and how much room it takes.
//
// A table like `kRecipes` and `kItems`, and APPEND-ONLY for the same reason: a
// save stores the kind index against every building, so inserting a row would
// turn somebody's mill into whatever moved into its slot.
//
// ONE ENTRY TODAY, deliberately. Both recipes in the game are mill recipes, so
// the thing worth building is a second mill — throughput, which is the genre's
// actual reason to construct. A bakery needs new items, new recipes and a new
// chain; that is content expansion and not what H7 is testing. The table's
// existence is the same pattern the other two catalogues use, not a claim that
// N kinds have ever been exercised.
#pragma once

#include <array>
#include <string_view>

#include "aether/core/types.hpp"
#include "hf/content/recipes.hpp"

namespace hearthfield::content {

using BuildingKind = aether::U16;

inline constexpr BuildingKind kMillKind = 0;

struct BuildingType {
  std::string_view name;
  // In CELLS, on the build lattice. Square is all this needs; a non-square
  // footprint would also need a rotation to be worth having, and rotation is a
  // second feature (ADR-0116 rejected free camera rotation on the same
  // "no art pass covers it" grounds).
  aether::U32 span = 1;
  aether::U32 cost = 0;
  // Drawn by view/. A path rather than a typed handle because `content` is the
  // sim's layer and may not link `resources` (games/CLAUDE.md law 4).
  std::string_view model;
  // Which recipes this kind can run. A building that could run anything would
  // make the kind decorative.
  aether::U32 recipe_count = 0;
  std::array<RecipeId, 4> recipes{};
};

inline constexpr std::array<BuildingType, 1> kBuildingTypes = {{
    BuildingType{.name = "mill",
                 .span = 2,
                 // Dear enough that a second mill is a decision, cheap enough
                 // that it is reachable in a session — LandPrice starts at 50
                 // and climbs, so this sits deliberately above the first few
                 // plots and below a long grind.
                 .cost = 320,
                 .model = "models/mill.gltf",
                 .recipe_count = 2,
                 .recipes = {kGrindWheat, kGrindCorn}},
}};

[[nodiscard]] constexpr bool BuildingKindExists(BuildingKind kind) {
  return kind < kBuildingTypes.size();
}

[[nodiscard]] constexpr const BuildingType& BuildingTypeOf(BuildingKind kind) {
  return kBuildingTypes[static_cast<aether::Usize>(kind)];
}

// Whether this kind of building may be handed this recipe. The production
// system asks before queueing, so a mill cannot be told to bake.
[[nodiscard]] constexpr bool KindRuns(BuildingKind kind, RecipeId recipe) {
  if (!BuildingKindExists(kind)) {
    return false;
  }
  const BuildingType& type = BuildingTypeOf(kind);
  for (aether::U32 i = 0; i < type.recipe_count; ++i) {
    if (type.recipes[i] == recipe) {
      return true;
    }
  }
  return false;
}

constexpr bool AllBuildingTypesWellFormed() {
  for (const BuildingType& type : kBuildingTypes) {
    if (type.span == 0 || type.model.empty() || type.recipe_count == 0 ||
        type.recipe_count > type.recipes.size()) {
      return false;
    }
    for (aether::U32 i = 0; i < type.recipe_count; ++i) {
      if (!RecipeExists(type.recipes[i])) {
        return false;
      }
    }
  }
  return true;
}
// A kind that runs no recipe is a decoration with a queue, and a kind naming a
// recipe that does not exist would queue into an empty table — both want
// catching here rather than in a player's economy.
static_assert(AllBuildingTypesWellFormed(), "a building type is malformed");

}  // namespace hearthfield::content
