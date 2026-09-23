// The frame-buffered event bus: the inter-feature and sim→view contract.
//
// Every payload lives HERE rather than in the feature that emits it, so
// runtime never depends on a feature (AGENTS.md dependency law 1). A
// feature's own events.hpp is an alias onto this vocabulary.
#pragma once

#include <variant>
#include <vector>

#include "aether/core/types.hpp"
#include "hf/content/crops.hpp"
#include "hf/content/islands.hpp"
#include "hf/content/items.hpp"
#include "hf/content/recipes.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/farm.hpp"
#include "hf/runtime/tick.hpp"

namespace hearthfield::runtime {

// A crop finished growing. Emitted on the step readiness is REACHED, including
// the step that absorbed an absence — so an overnight gap produces one of
// these per plot, not one per tick waited.
struct CropReady {
  PlotId plot = kNoPlot;
  content::CropId crop = 0;
};

// A ready plot was harvested. `amount` is the crop's yield; production and
// economy read it from here rather than by asking plots.
struct Harvested {
  PlotId plot = kNoPlot;
  content::CropId crop = 0;
  aether::U32 amount = 0;
};

struct Sown {
  PlotId plot = kNoPlot;
  content::CropId crop = 0;
};

// ---- the chain (H3) --------------------------------------------------------

// Items reached the barn. `lost` is what did NOT fit — reported rather than
// silently dropped, because a player whose harvest vanished into a full barn
// deserves to be told which is why the cap exists.
struct Deposited {
  content::ItemId item = 0;
  aether::U32 stored = 0;
  aether::U32 lost = 0;
};

struct Queued {
  aether::U32 building = 0;
  content::RecipeId recipe = 0;
};

struct Produced {
  aether::U32 building = 0;
  content::RecipeId recipe = 0;
};

struct OrderPosted {
  aether::U8 slot = 0;
};

struct OrderFilled {
  aether::U8 slot = 0;
  aether::U32 reward = 0;
};

struct LandBought {
  PlotId plot = kNoPlot;
  aether::U32 cost = 0;
};

// ---- the coop (H5) ---------------------------------------------------------

// The trough was filled: `until` is the absolute tick the feed runs out. The
// COMMIT moment, and the reason an absence stays closed-form (ADR-0108).
struct Fed {
  aether::U32 spent = 0;
  Tick until = 0;
};

// Eggs arrived. `laid` counts the whole catch-up, so one of these covers a
// night away rather than one per interval — the same rule CropReady follows.
struct Laid {
  aether::U32 laid = 0;
};

// The trough ran dry and the birds have stopped. Emitted ONCE, on the step that
// crosses `fed_until`, so view can say so rather than nagging every tick.
struct CoopHungry {};

// A player action that could not happen — no coin, no room, no ingredients.
// ONE event rather than four, because view's response is the same in every
// case: say why, briefly, where the player was looking.
struct Refused {
  enum class Why : aether::U8 {
    kNoRoom,
    kNoItems,
    kNoCoin,
    kNoSpaceInQueue,
    // H7. A placement can fail for a reason that is neither money nor capacity
    // — the ground was not legal — and collapsing that into kNoRoom would tell
    // the player "no space" when what they need to hear is "not there".
    kBadGround,
  };
  Why why = Why::kNoRoom;
};

// A building went up. Carries WHAT and WHERE as well as the id, so view/ can
// spawn its mesh from the event without reaching into the world (the sim→view
// contract in AGENTS.md).
struct BuildingPlaced {
  aether::U32 building = 0;
  content::BuildingKind kind = 0;
  BuildCell cell{};
};

// ---- the archipelago (sky world s7) ----------------------------------------

// An island became the player's. `cost` because the HUD says what was spent,
// the same as LandBought.
struct IslandBought {
  content::IslandId island = 0;
  aether::U32 cost = 0;
};

// The player is now on a different island. THE SIM'S DECISION, emitted the step
// it is made — app/ turns it into a two-second crossing, which is presentation
// over a fact that was instant.
struct Departed {
  content::IslandId from = 0;
  content::IslandId to = 0;
};

using GameEvent =
    std::variant<CropReady, Harvested, Sown, Deposited, Queued, Produced,
                 OrderPosted, OrderFilled, LandBought, Fed, Laid, CoopHungry,
                 Refused, BuildingPlaced, IslandBought, Departed>;
using EventList = std::vector<GameEvent>;

}  // namespace hearthfield::runtime
