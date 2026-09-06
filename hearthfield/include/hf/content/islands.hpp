// The archipelago — every island the game can put a player on, as data.
//
// GEA §16.4.2's hub-and-spoke, which is the shape Jak 2 used: a hub area with
// offshoot areas reached through an air lock. An island is the unit of content,
// of save and of LOAD, so this table is what the game consults BEFORE deciding
// to load anything — which chunk, which zone, and whether it is open yet. The
// chunk itself describes what to spawn and knows none of this.
//
// A chunk that every island shares is not here at all: the camera, the sun and
// the cloud sea are `worlds/persistent.world.json`, GEA's
// load-and-stay-resident data, loaded once and never unloaded when travel swaps
// an island out.
#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "hf/content/camera.hpp"

namespace hearthfield::content {

using IslandId = aether::U16;

// The persistent chunk: the camera, the sun and the sky. Loaded first and never
// unloaded, which is what lets s5 free the island the player is leaving without
// taking their viewpoint with it.
inline constexpr std::string_view kPersistentPath =
    "worlds/persistent.world.json";

struct Island {
  // For logs and for the save's own sake — an id is an index, and an index in a
  // crash report tells you nothing.
  std::string_view name;
  // The island itself: ground and tree cover, the things a PLACE is made of.
  std::string_view chunk;
  // What is built on it, loaded after the island. EMPTY when nothing is —
  // which is how a satellite says "bare rock" without app/ needing a branch.
  std::string_view zone;

  // WHERE IT FLOATS, in world metres, and THE HUB IS THE ORIGIN. Everything
  // already authored — the board's lattice, the build ring, the steading's
  // positions — is written around 0,0,0, so moving the hub is a re-layout of
  // all of it and not an edit here (the lesson kDefaultColumns taught at
  // 8->24).
  //
  // Kept inside the CLOUD SEA, which is 284 x 300 m about the origin: an island
  // at 90 m with a 37.5 m half-extent reaches 128 m and still has sky under it.
  // At play zoom (half-height 20) a neighbour 90 m away is off screen entirely;
  // at the 45 ceiling it comes into frame at the edge, which is what makes it a
  // distant silhouette rather than clutter.
  aether::Vec3 world_pos;
  // The silhouette THIS island presents to the others. Only one island is
  // resident at full detail; the rest stand at their positions as this, so the
  // archipelago is visible without being loaded.
  //
  // island.gltf for all three, at 560 triangles against hub_island.glb's 7,626.
  // For the satellites it is also their detail mesh, so proxy and real form are
  // the same shape. For the HUB it is not — a parametric disc where the real
  // thing is a sculpt — and that difference is a knowingly accepted one at 90 m
  // under a parallel projection. The first person to notice it should author a
  // hub proxy rather than raise the detail here.
  std::string_view proxy_mesh;

  // The ground mesh, and the two half-extents its roam box is cut from. THESE
  // ARE THE ASSET'S NUMBERS, and until 2026-08-29 the copy in camera.hpp said
  // so in a comment and nothing else: "change one and the other is silently
  // wrong". `tools/model_contract.json` records the same mesh's size and
  // `models-check` gates it, so islands_test now asserts the pair agrees. The
  // duplication remains; what is gone is its silence.
  std::string_view ground_mesh;
  aether::F32 half_extent_x_m;
  aether::F32 half_extent_z_m;
  // How far short of the rim the FOCUS is held. Centre the view exactly on the
  // edge and half the screen is sky, which reads as being lost rather than as a
  // vista — so the focus stops inside while the VIEW is free to run off, which
  // is the inversion the sky world is built on (camera.hpp's CameraBounds).
  aether::F32 roam_inset_m;

  // Per-island framing, and the price of one persistent camera. With a single
  // authored camera every island would otherwise be framed identically forever;
  // this is the override that makes a satellite at a different scale possible.
  // NULLOPT means "keep what the camera authored", which is what the hub wants
  // and what the rig already does — it ADOPTS authored framing
  // (camera_rig.hpp).
  std::optional<aether::F32> ortho_half_height;

  // Zero is open from the start. The RULE is not here on purpose: with one
  // island and nothing to gate, an unlock predicate would be a mechanism with
  // no case to serve. s4 owns the rule, the save state and the migration.
  aether::U32 unlock_coins = 0;

  // The roam box, cut from the ground mesh rather than authored a second time,
  // and CENTRED ON THE ISLAND rather than on the origin — since s5 an island
  // stands where it floats, so only the hub's box is about 0,0.
  [[nodiscard]] constexpr CameraBounds Roam() const {
    return CameraBounds{.min_x = world_pos.x - half_extent_x_m + roam_inset_m,
                        .max_x = world_pos.x + half_extent_x_m - roam_inset_m,
                        .min_z = world_pos.z - half_extent_z_m + roam_inset_m,
                        .max_z = world_pos.z + half_extent_z_m - roam_inset_m};
    // No `field_half_extent`: the view is MEANT to leave the island. On the
    // 120 m slab it was mandatory because past the edge was void.
  }
};

// ISLAND IDS ARE APPEND-ONLY, and this is a save-format rule rather than a
// stylistic one — the same rule content::kCrops carries and for the same
// reason. s4 puts the unlocked set in the save as indices into this table, so
// reordering it silently moves a player's progress to a different island. Add
// at the end; to retire an island, leave the row and stop offering it.
inline constexpr IslandId kHub = 0;
inline constexpr IslandId kNearIsle = 1;
inline constexpr IslandId kFarIsle = 2;

inline constexpr std::array<Island, 3> kIslands = {{
    Island{.name = "hub",
           .chunk = "worlds/islands/hub.world.json",
           .zone = "worlds/farm.world.json",
           .world_pos = {0.0f, 0.0f, 0.0f},
           .proxy_mesh = "models/island.gltf",
           .ground_mesh = "models/hub_island.glb",
           // hub_island.glb is 75.0 x 74.737 m in X and Z. SYMMETRIC since the
           // 2026-08-28 swap, and that is the point of it: the replacement
           // sculpt's plateau is genuinely central, so the farm pad, the origin
           // and the island's centre are one point. The previous sculpt's
           // middle was a river gorge and these were four different numbers.
           .half_extent_x_m = 37.5f,
           .half_extent_z_m = 37.37f,
           .roam_inset_m = 6.0f,
           .ortho_half_height = std::nullopt,
           .unlock_coins = 0},
    // THE TWO SATELLITES. Bare rock when s4 introduced them, and FARMS since
    // the second-farm plan: both carry the same thin zone — a fence and nothing
    // else — because both were ratified as the same 24x24 board. What differs
    // between them is the id, the position and the PRICE.
    //
    // `zone` REMAINS OPTIONAL and nothing here makes it less so: an island with
    // an empty one is still the way to say "bare rock", and app/ still has no
    // branch for it. It is simply that no island uses that today.
    //
    // The parametric island.gltf is what the hub swap kept back for exactly
    // this (560 triangles against 7,626).
    //
    // island.gltf is 74.98 x 73.87 m, so a satellite is nearly a hub in
    // footprint. NOT SCALED DOWN, deliberately: the registry's half-extents are
    // checked against model_contract.json's `size_m`, and a scale in the chunk
    // would quietly make the two sides mean different things.
    Island{.name = "near isle",
           .chunk = "worlds/islands/near_isle.world.json",
           .zone = "worlds/satellite_farm.world.json",
           // Off the hub's +X shoulder and slightly BELOW it: a flat plane of
           // islands reads as a floor, and the whole brief is that these float.
           .world_pos = {90.0f, -8.0f, 15.0f},
           .proxy_mesh = "models/island.gltf",
           .ground_mesh = "models/island.gltf",
           .half_extent_x_m = 37.49f,
           .half_extent_z_m = 36.935f,
           .roam_inset_m = 6.0f,
           .ortho_half_height = std::nullopt,
           .unlock_coins = 250},
    Island{.name = "far isle",
           .chunk = "worlds/islands/far_isle.world.json",
           // THE SAME ZONE FILE as the near isle, not a copy of it. Both were
           // ratified as the same 24x24 farm, so one authored zone serves both
           // and cannot drift from itself; a satellite that earns its own shape
           // gets its own file and leaves the other on this one.
           .zone = "worlds/satellite_farm.world.json",
           // Behind and ABOVE, so the two satellites are not a mirrored pair.
           .world_pos = {-25.0f, 6.0f, -90.0f},
           .proxy_mesh = "models/island.gltf",
           .ground_mesh = "models/island.gltf",
           .half_extent_x_m = 37.49f,
           .half_extent_z_m = 36.935f,
           .roam_inset_m = 6.0f,
           .ortho_half_height = std::nullopt,
           // Dearer than the near isle, so the ladder has a shape rather than
           // two rungs at one height. The FIGURE is provisional — nothing
           // balances it, and no test can: pacing is the thing this game has
           // never had an oracle for (the field redesign's standing risk).
           .unlock_coins = 900},
}};

// THE BITMASK'S CEILING, and it is here rather than in the save because this is
// the table that would breach it. runtime::Archipelago stores the unlocked set
// as one U32 with bit i = island i — fixed width, so the save's geometry does
// NOT depend on this table's length. That is the lesson kItemCountV2 records in
// blood: v2 wrote the barn as a bare run of `kItemCount` counts with no length,
// which made the catalogue's SIZE part of the format and would have corrupted
// every file on disk when an item was appended. A 33rd island is the one change
// that costs a save version, and it fails the build here rather than silently
// dropping an island's unlock state.
static_assert(kIslands.size() <= 32,
              "the unlocked set is a U32 bitmask — a 33rd island needs a wider "
              "field and therefore a new save version");

[[nodiscard]] constexpr bool IslandExists(IslandId id) {
  return id < kIslands.size();
}

// Absent rather than clamped: asking for an island that is not in the table is
// a content bug, and returning the hub would hide it behind a playable game.
[[nodiscard]] constexpr const Island* FindIsland(IslandId id) {
  return IslandExists(id) ? &kIslands[id] : nullptr;
}

}  // namespace hearthfield::content
