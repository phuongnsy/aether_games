// One fixed step's input, latched once per render frame (the input-latch rule
// in games/CLAUDE.md). The replay unit: a run is a sequence of these.
#pragma once

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "hf/content/crops.hpp"
#include "hf/content/islands.hpp"
#include "hf/content/items.hpp"
#include "hf/content/recipes.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/farm.hpp"

namespace hearthfield::runtime {

struct LatchedInput {
  // Ticks that passed OUTSIDE the process, computed in app/ from the clock —
  // the only layer that may read one — and handed to the step as data, exactly
  // like a button press (spec §4.2). Because it is recorded alongside every
  // other latched value, a replay reproduces overnight growth to the tick.
  //
  // U32 rather than Tick: this is a per-resume delta, and 828 days of a single
  // absence at 60 Hz is a bound no player reaches. The world counter it feeds
  // is U64 because that one accumulates.
  aether::U32 offline_ticks = 0;

  bool tap = false;  // rising edge, latched this step
  aether::Vec2 tap_ndc{0.0f, 0.0f};
  // The plot under the pointer, resolved in app/ by ScreenRay at H1. Resolved
  // OUTSIDE the step for the same reason the clock is: picking needs a
  // viewport, and the sim must not know what a viewport is.
  PlotId hovered = kNoPlot;

  // ---- everything the player can DO ----------------------------------------
  //
  // Latched, all of it. Not because a struct is tidy, but because a replay
  // records exactly this: an action reaching the world by any other route
  // leaves no trace in the stream, so the session stops reproducing while H0's
  // replay oracle and H2's save-across-a-gap both stay green — they only ever
  // replay what was recorded. `ui::Screen`s write these through
  // runtime::GameApi and touch nothing else (H4 plan §3b).
  static constexpr aether::U8 kNoSlot = 0xFF;
  static constexpr content::RecipeId kNoRecipe = 0xFFFF;
  static constexpr content::ItemId kNoItem = 0xFFFF;

  // What a tap plants. Carried here rather than held by the plots system, so
  // the seed choice is part of the recorded stream like everything else.
  content::CropId sow_crop = 0;

  // ONE RecipeId, not the pair of bools H3 used as a stand-in for it.
  content::RecipeId queue_recipe = kNoRecipe;
  aether::U32 queue_building = 0;

  // BUILD. The kind is a sentinel when nothing is being placed, so a step with
  // no build in it costs one comparison — and, like every other verb here, a
  // placement reaches the world ONLY through this struct, which is what keeps
  // it in the replay stream and in the save-across-a-gap.
  //
  // The CELL is resolved in app/ for the same reason `hovered` is: turning a
  // pointer into a ground position needs a viewport, and the sim must not know
  // what a viewport is.
  static constexpr content::BuildingKind kNoBuilding = 0xFFFF;
  content::BuildingKind place_kind = kNoBuilding;
  BuildCell place_cell{};

  aether::U8 fill_slot = kNoSlot;
  bool buy_land = false;
  bool buy_barn = false;
  content::ItemId buy_item = kNoItem;
  aether::U32 buy_count = 0;
  // Fill the trough. A BOOL rather than an amount: the fill size is content
  // (content::kFeedPerFill), and a player-chosen quantity would be a second
  // thing to record for no design gain.
  bool feed_coop = false;

  // --- the archipelago (sky world s7) ---------------------------------------
  //
  // BOTH GO THROUGH THE LATCH, and travel especially: `Isles().current` is
  // world state and is in the digest, so app/ moving it directly would work
  // perfectly and silently stop the session reproducing — the exact failure
  // game_api.hpp's opening comment is about. It DID move it directly between s5
  // and s7, which is how this pair came to exist.
  //
  // The sim decides WHERE YOU ARE; app/ animates getting there. The crossing is
  // two seconds of presentation over a decision that was instant, the same
  // division a crop's growth already makes.
  static constexpr content::IslandId kNoIsland = 0xFFFF;
  content::IslandId unlock_island = kNoIsland;
  content::IslandId travel_to = kNoIsland;
};

}  // namespace hearthfield::runtime
