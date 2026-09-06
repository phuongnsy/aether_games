// The sim→view data contract: GameWorld produces this each step, and view/
// consumes ONLY this plus the drained events, never live sim state.
#pragma once

#include <array>
#include <vector>

#include "aether/core/types.hpp"
#include "hf/content/crops.hpp"
#include "hf/content/islands.hpp"
#include "hf/content/items.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/farm.hpp"
#include "hf/runtime/tick.hpp"

namespace hearthfield::runtime {

struct PlotView {
  PlotState state = PlotState::kEmpty;
  content::CropId crop = 0;
  aether::F32 growth = 0.0f;  // 0..1, what decides the mesh's growth stage
};

// One building, as view/ needs to draw it. At namespace scope beside PlotView
// and for the same reason: view/ names the type, and a member type would make
// every mention of it carry the snapshot's name.
struct BuildingView {
  content::BuildingKind kind = 0;
  BuildCell cell{};
  bool busy = false;
};

struct ViewSnapshot {
  Tick tick = 0;
  aether::U32 ready_count = 0;  // the HUD's "something needs you" number
  std::vector<PlotView> plots;

  // The chain, as view needs to SHOW it — never as live sim state. `unlocked`
  // is here rather than a per-plot flag because that is how the sim stores it
  // (one number in the purse), and duplicating it per plot would be a second
  // place for it to be wrong.
  aether::U32 unlocked = 0;
  aether::U32 coin = 0;
  aether::U32 barn_total = 0;
  aether::U32 barn_capacity = 0;
  aether::U32 milling = 0;  // items queued at the mill

  // The archipelago as the shop needs to SHOW it (sky world s7): which islands
  // are owned, and which one the player is on. Copied rather than reached for,
  // like everything else here — a screen reads the snapshot and never the
  // world.
  aether::U32 islands_unlocked = 1;
  content::IslandId island = 0;

  // H7: WHERE the buildings stand. view/ spawns a mesh per entry, so a
  // placement becomes visible through the SNAPSHOT rather than through an
  // event — the one channel from sim to view, so a view that missed an event
  // still cannot disagree about what exists.
  std::vector<BuildingView> buildings;

  // The coop. `fed_ticks` is what remains rather than the absolute deadline,
  // because the HUD's question is "how long have I got", and subtracting a tick
  // from another tick in view/ would be the sim's arithmetic done twice.
  aether::U8 animals = 0;
  bool coop_fed = false;
  Tick fed_ticks = 0;
  bool raining = false;

  // What the SCREENS show. Here rather than read from the world directly, so
  // the sim -> view contract stays the single channel and a screen structurally
  // cannot mutate what it is displaying.
  std::array<aether::U32, content::kItemCount> items{};
  std::array<Order, kOrderSlots> orders{};
  // NO PRICES HERE. They come from economy's progression curve, and `runtime`
  // may not depend on a feature — the shop screen lives in app/, which sees
  // everything, and calls economy::LandPrice/BarnPrice itself.
};

}  // namespace hearthfield::runtime
