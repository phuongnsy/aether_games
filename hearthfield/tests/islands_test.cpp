// The archipelago registry, and the two files it must not drift from.
//
// content::kIslands is the table app/ walks instead of knowing a path, so every
// claim in it is a claim about a file on disk: that this chunk exists, that it
// spawns the ground mesh the registry names, and that the mesh is the size the
// roam box was cut from. None of those is checked by a compiler and only the
// last one is visible on screen — a roam box a few metres too generous lets the
// focus off the rim, which looks like a camera bug rather than like a number.
#include "hf/content/islands.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "aether/core/config.hpp"

using namespace aether;
using namespace hearthfield;

namespace {

[[nodiscard]] std::string ReadOrFail(const std::filesystem::path& path) {
  std::ifstream file(path);
  REQUIRE_MESSAGE(file.good(), "missing file: " << path.string());
  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] std::filesystem::path AssetPath(std::string_view relative) {
  return std::filesystem::path(HEARTHFIELD_ASSET_DIR) / relative;
}

// Every `model` an entity in this chunk spawns, in order.
[[nodiscard]] std::vector<std::string> ModelsIn(const Config& chunk) {
  std::vector<std::string> models;
  const auto count = chunk.ArraySize("entities");
  if (!count) {
    return models;
  }
  for (Usize i = 0; i < *count; ++i) {
    const std::string entity = "entities." + std::to_string(i);
    if (auto model = chunk.GetString(entity + ".model")) {
      models.emplace_back(*model);
    }
  }
  return models;
}

}  // namespace

TEST_CASE("EVERY ISLAND'S CHUNKS EXIST AND PARSE") {
  for (const content::Island& island : content::kIslands) {
    CAPTURE(island.name);
    const auto chunk = ParseJsonConfig(ReadOrFail(AssetPath(island.chunk)));
    if (!chunk) {
      FAIL("island chunk would not parse: ", chunk.error().message);
    }
    // A zone is OPTIONAL — that is how an island with nothing built on it is
    // expressed — but a DECLARED one that is not there is a content bug, and
    // app/ fails the load rather than playing on.
    if (!island.zone.empty()) {
      const auto zone = ParseJsonConfig(ReadOrFail(AssetPath(island.zone)));
      if (!zone) {
        FAIL("island zone would not parse: ", zone.error().message);
      }
    }
  }
}

TEST_CASE("THE PERSISTENT CHUNK CARRIES EXACTLY ONE CAMERA AND ONE SUN") {
  // The load-and-stay-resident chunk (GEA §16.4.2). ONE camera, because app/
  // takes the first it finds and a second would be chosen by file order; ONE
  // directional light, because two would either double the shadow pass or let
  // one silently win. Both are invariants of the s3 split rather than of the
  // engine — nothing in `resources` objects to a chunk with four cameras.
  const auto chunk =
      ParseJsonConfig(ReadOrFail(AssetPath(content::kPersistentPath)));
  if (!chunk) {
    FAIL("the persistent chunk would not parse: ", chunk.error().message);
  }
  const auto count = chunk->ArraySize("entities");
  REQUIRE(count.has_value());
  int cameras = 0;
  int suns = 0;
  for (Usize i = 0; i < *count; ++i) {
    const auto type =
        chunk->GetString("entities." + std::to_string(i) + ".type");
    if (!type) {
      continue;
    }
    cameras += static_cast<int>(*type == "camera");
    suns += static_cast<int>(*type == "directional_light");
  }
  CHECK(cameras == 1);
  CHECK(suns == 1);
}

TEST_CASE("AN ISLAND'S CHUNK SPAWNS THE GROUND MESH THE REGISTRY NAMES") {
  // The registry's `ground_mesh` is what the roam box below is checked against,
  // so it has to be the mesh actually standing there. Swap the model in the
  // chunk and leave the registry alone and every extent check downstream is
  // measuring a mesh that is no longer in the world.
  for (const content::Island& island : content::kIslands) {
    CAPTURE(island.name);
    const auto chunk = ParseJsonConfig(ReadOrFail(AssetPath(island.chunk)));
    REQUIRE(chunk.has_value());
    const std::vector<std::string> models = ModelsIn(*chunk);
    CHECK_MESSAGE(std::find(models.begin(), models.end(),
                            std::string(island.ground_mesh)) != models.end(),
                  "island '" << island.name
                             << "' does not spawn its own ground "
                             << "mesh " << island.ground_mesh);
  }
}

TEST_CASE("AN ISLAND'S HALF-EXTENTS AGREE WITH ITS MODEL CONTRACT") {
  // THE DUPLICATION THIS TEST EXISTS FOR. Until 2026-08-29 these four numbers
  // sat in camera.hpp under a comment reading "DUPLICATED FROM THE ASSET —
  // change one and the other is silently wrong", and nothing checked it.
  // tools/model_contract.json records the same mesh's size and `models-check`
  // gates THAT against the glTF, so asserting the registry against the contract
  // chains the registry to the asset without this test having to open a .glb.
  const auto contract = ParseJsonConfig(
      ReadOrFail(std::filesystem::path(HEARTHFIELD_MODEL_CONTRACT)));
  if (!contract) {
    FAIL("model_contract.json would not parse: ", contract.error().message);
  }
  const auto count = contract->ArraySize("models");
  REQUIRE(count.has_value());

  for (const content::Island& island : content::kIslands) {
    CAPTURE(island.name);
    bool found = false;
    for (Usize i = 0; i < *count; ++i) {
      const std::string entry = "models." + std::to_string(i);
      const auto path = contract->GetString(entry + ".path");
      if (!path || !path->ends_with(island.ground_mesh)) {
        continue;
      }
      found = true;
      const auto size_x = contract->GetNumber(entry + ".size_m.0");
      const auto size_z = contract->GetNumber(entry + ".size_m.2");
      // The contract's own tolerance, not a new one: it is what models-check
      // allows the glTF to differ by, so a tighter bound here would fail on a
      // mesh the gate calls good.
      const auto tolerance = contract->GetNumber(entry + ".tolerance_m");
      REQUIRE(size_x.has_value());
      REQUIRE(size_z.has_value());
      REQUIRE(tolerance.has_value());
      CHECK(std::abs(2.0 * island.half_extent_x_m - *size_x) <= *tolerance);
      CHECK(std::abs(2.0 * island.half_extent_z_m - *size_z) <= *tolerance);
      break;
    }
    CHECK_MESSAGE(found, "island '"
                             << island.name << "' names a ground mesh "
                             << island.ground_mesh
                             << " that model_contract.json does not cover, so "
                                "nothing gates its size");
  }
}

TEST_CASE("A ROAM BOX IS INSIDE ITS ISLAND AND HAS ROOM TO MOVE IN") {
  for (const content::Island& island : content::kIslands) {
    CAPTURE(island.name);
    const content::CameraBounds roam = island.Roam();
    // Inside the rim, which is what the inset is for: centre the view exactly
    // on the edge and half the screen is sky. CENTRED ON THE ISLAND since s5 —
    // an island stands where it floats, so only the hub's box is about 0,0 and
    // comparing against bare half-extents would pass by accident there and fail
    // everywhere else.
    CHECK(roam.min_x > island.world_pos.x - island.half_extent_x_m);
    CHECK(roam.max_x < island.world_pos.x + island.half_extent_x_m);
    CHECK(roam.min_z > island.world_pos.z - island.half_extent_z_m);
    CHECK(roam.max_z < island.world_pos.z + island.half_extent_z_m);
    // And not inset past itself — an island smaller than twice its inset would
    // produce an inverted box, which CameraBounds::ClampX would silently
    // tolerate (it sorts its own arguments) and which would pin the focus to a
    // point rather than failing.
    CHECK(roam.max_x > roam.min_x);
    CHECK(roam.max_z > roam.min_z);
    // NO field extent, ever: the view is MEANT to leave the island. This is the
    // inversion the sky world is built on, and re-introducing one here would
    // clamp the rim back out of frame.
    CHECK_FALSE(roam.field_half_extent.has_value());
  }
}

TEST_CASE("THE ISLAND TABLE IS INDEXED BY ID") {
  // Ids are indices and s4 puts them in the save, so this is a save-format
  // check as much as a lookup one — the same rule content::kCrops carries.
  CHECK(content::kHub == 0);
  CHECK(content::IslandExists(content::kHub));
  REQUIRE(content::FindIsland(content::kHub) != nullptr);
  CHECK(content::FindIsland(content::kHub)->name == "hub");
  // Absent rather than clamped: asking for an island that is not there is a
  // content bug, and returning the hub would hide it behind a playable game.
  CHECK_FALSE(content::IslandExists(
      static_cast<content::IslandId>(content::kIslands.size())));
  CHECK(content::FindIsland(static_cast<content::IslandId>(
            content::kIslands.size())) == nullptr);
}

TEST_CASE("NO TWO ISLANDS OVERLAP") {
  // They are placed by hand in the registry, and two islands intersecting would
  // read as one broken island rather than as a coordinate typo. Compared on
  // the X/Z footprint only: they sit at different heights ON PURPOSE, so a
  // vertical gap is not what keeps them apart.
  for (Usize a = 0; a < content::kIslands.size(); ++a) {
    for (Usize b = a + 1; b < content::kIslands.size(); ++b) {
      const content::Island& one = content::kIslands[a];
      const content::Island& two = content::kIslands[b];
      CAPTURE(one.name);
      CAPTURE(two.name);
      const F32 dx = one.world_pos.x - two.world_pos.x;
      const F32 dz = one.world_pos.z - two.world_pos.z;
      const F32 gap = std::sqrt(dx * dx + dz * dz);
      // Half-extents are the bounding box, so the sum is the worst case: two
      // discs that clear it cannot touch whatever their orientation.
      const F32 touching = std::max(one.half_extent_x_m, one.half_extent_z_m) +
                           std::max(two.half_extent_x_m, two.half_extent_z_m);
      CHECK_MESSAGE(gap > touching, "islands '" << one.name << "' and '"
                                                << two.name << "' are " << gap
                                                << " m apart but " << touching
                                                << " m would touch");
    }
  }
}

TEST_CASE("EVERY ISLAND FLOATS OVER THE CLOUD SEA") {
  // The clouds are ONE baked mesh of finite extent, not an infinite plane, so
  // an island placed past its edge floats over empty sky — which reads as the
  // world running out rather than as a vista. Checked against the same
  // model_contract.json entry `models-check` gates, exactly as the half-extents
  // are: the sea's size is the asset's, not a number repeated here.
  const auto contract = ParseJsonConfig(
      ReadOrFail(std::filesystem::path(HEARTHFIELD_MODEL_CONTRACT)));
  REQUIRE(contract.has_value());
  const auto count = contract->ArraySize("models");
  REQUIRE(count.has_value());

  std::optional<F64> sea_x;
  std::optional<F64> sea_z;
  for (Usize i = 0; i < *count; ++i) {
    const std::string entry = "models." + std::to_string(i);
    const auto path = contract->GetString(entry + ".path");
    if (path && path->ends_with("clouds.gltf")) {
      sea_x = contract->GetNumber(entry + ".size_m.0");
      sea_z = contract->GetNumber(entry + ".size_m.2");
      break;
    }
  }
  REQUIRE_MESSAGE(sea_x.has_value(),
                  "clouds.gltf is not in model_contract.json, so nothing gates "
                  "the extent every island is placed inside");
  REQUIRE(sea_z.has_value());

  for (const content::Island& island : content::kIslands) {
    CAPTURE(island.name);
    // The island's far RIM, not its centre: an island whose middle is over
    // cloud and whose edge is not still shows the gap.
    const F64 reach_x = std::abs(island.world_pos.x) + island.half_extent_x_m;
    const F64 reach_z = std::abs(island.world_pos.z) + island.half_extent_z_m;
    CHECK(reach_x <= *sea_x * 0.5);
    CHECK(reach_z <= *sea_z * 0.5);
  }
}
