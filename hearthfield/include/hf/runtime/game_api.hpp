// GameApi — the narrow verb seam a screen drives the game through.
//
// EVERY VERB SETS A LATCHED FIELD AND RETURNS. None of them touch the world,
// and that restraint is the whole point rather than tidiness: a button that
// called into the sim would work perfectly and silently end the replay. The
// session would stop reproducing while H0's replay oracle and H2's
// save-across-a-gap both stayed green, because they only ever replay what was
// recorded — and nothing records a method call.
//
// `games/CLAUDE.md`: "Screens (in app/) drive flow through a narrow GameApi
// (defined in runtime) with exactly the verbs needed". H0 deliberately did not
// front-load one; H4 is the first screen that needs it.
#pragma once

#include "aether/core/types.hpp"
#include "hf/content/crops.hpp"
#include "hf/content/items.hpp"
#include "hf/content/recipes.hpp"
#include "hf/runtime/latched_input.hpp"

namespace hearthfield::runtime {

class GameApi {
 public:
  explicit GameApi(LatchedInput& pending) : pending_(&pending) {}

  // What the next tap plants. Sticky rather than one-shot: it is a mode the
  // player chose, and re-sending it every frame would be the same value.
  void SelectCrop(content::CropId crop) { pending_->sow_crop = crop; }
  [[nodiscard]] content::CropId SelectedCrop() const {
    return pending_->sow_crop;
  }

  void Queue(aether::U32 building, content::RecipeId recipe) {
    pending_->queue_building = building;
    pending_->queue_recipe = recipe;
  }
  void Fill(aether::U8 slot) { pending_->fill_slot = slot; }
  void BuyLand() { pending_->buy_land = true; }
  void BuyBarn() { pending_->buy_barn = true; }
  void BuyItem(content::ItemId item, aether::U32 count) {
    pending_->buy_item = item;
    pending_->buy_count = count;
  }
  void Feed() { pending_->feed_coop = true; }

  // Put one down. The CELL comes from app/, which is the only layer that can
  // turn a pointer into a place on the ground — the same division `hovered`
  // already makes for plots.
  void Place(content::BuildingKind kind, BuildCell cell) {
    pending_->place_kind = kind;
    pending_->place_cell = cell;
  }

  // Buy an island, and cross to one (sky world s7). Latched like everything
  // else here — and travel especially, because `Isles().current` is world state
  // and is in the digest, so a screen that moved it directly would work
  // perfectly and silently stop the session reproducing.
  void BuyIsland(content::IslandId island) { pending_->unlock_island = island; }
  void TravelTo(content::IslandId island) { pending_->travel_to = island; }

 private:
  LatchedInput* pending_;  // borrowed; app/ owns the latch and clears it
};

}  // namespace hearthfield::runtime
