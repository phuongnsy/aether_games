#include "hf/features/placement/system.hpp"

#include "hf/content/buildings.hpp"
#include "hf/runtime/build_ring.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::placement {

// The save refuses a farm with more than 64 buildings, so the sim must refuse
// to make one. Checked here rather than trusted, because the failure would land
// at the next autosave — a player builds a 65th mill and their farm stops
// loading, which is the worst possible place to discover a bound.
inline constexpr aether::Usize kMaxBuildings = 64;

void PlacementSystem::Step(runtime::StepContext& ctx,
                           const runtime::EventList& /*in*/,
                           runtime::EventList& out) {
  const content::BuildingKind kind = ctx.input.place_kind;
  if (kind == runtime::LatchedInput::kNoBuilding) {
    return;
  }
  if (!content::BuildingKindExists(kind)) {
    out.emplace_back(
        runtime::Refused{.why = runtime::Refused::Why::kBadGround});
    return;
  }

  // GROUND FIRST, then money. A player dragging a ghost over the field is
  // told "not there" whether or not they could afford it, and being told "no
  // coin" while standing on an illegal cell would send them off to earn money
  // for a placement that was never going to work.
  const runtime::BuildRing ring;
  const runtime::PlaceRefusal verdict = runtime::CanPlace(
      ring, kind, ctx.input.place_cell, ctx.world.Buildings());
  if (verdict != runtime::PlaceRefusal::kOk) {
    out.emplace_back(
        runtime::Refused{.why = runtime::Refused::Why::kBadGround});
    return;
  }
  if (ctx.world.Buildings().size() >= kMaxBuildings) {
    out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoRoom});
    return;
  }

  runtime::Purse& purse = ctx.world.ThePurse();
  const aether::U32 cost = content::BuildingTypeOf(kind).cost;
  if (purse.coin < cost) {
    out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoCoin});
    return;
  }

  purse.coin -= cost;
  const aether::U32 id = ctx.world.AddBuilding(kind, ctx.input.place_cell);
  out.emplace_back(runtime::BuildingPlaced{
      .building = id, .kind = kind, .cell = ctx.input.place_cell});
}

}  // namespace hearthfield::placement
