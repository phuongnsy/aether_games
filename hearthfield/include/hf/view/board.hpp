// The board: the scene nodes that show the sim's plot table.
//
// Consumes the ViewSnapshot and nothing else (AGENTS.md dependency law
// 2) — it never sees a feature, and the sim never learns that meshes exist.
#pragma once

#include <vector>

#include "aether/app/pbr_material_factory.hpp"
#include "aether/core/error.hpp"
#include "aether/resources/mesh.hpp"
#include "aether/resources/resource_manager.hpp"
#include "aether/scene_core/scene.hpp"
#include "hf/runtime/grid.hpp"
#include "hf/runtime/snapshot.hpp"

namespace hearthfield::view {

class Board {
 public:
  // Builds every node once. The grid does not change size at runtime, so this
  // is the only allocation the board ever does — Update writes transforms into
  // nodes that already exist.
  [[nodiscard]] aether::Result<void> Create(
      aether::resources::ResourceManager& resources,
      aether::app::PbrMaterialFactory& materials, aether::scene::Scene& scene,
      const runtime::Grid& grid, aether::scene::NodeId parent);

  // Give every node back and forget them (sky world s5). Travel unloads the
  // island the farm stands on, and node ids left behind would be STALE rather
  // than merely unused — the next Update would write transforms into slots the
  // scene has already handed to something else. The meshes and materials are
  // deliberately KEPT: they are the same on return, and dropping the handles
  // would re-import the assets on every crossing.
  void Clear(aether::scene::Scene& scene);

  // Mirror one snapshot onto the scene.
  void Update(aether::scene::Scene& scene, const runtime::Grid& grid,
              const runtime::ViewSnapshot& snapshot);

  // Darken the soil. `MeshItem::wetness` drops roughness and darkens albedo,
  // and 0 is exactly identity — so a dry farm renders bit-for-bit as it did
  // before H5, which is what keeps the existing capture digests meaningful.
  void SetWetness(aether::scene::Scene& scene, aether::F32 wetness);

 private:
  // One node per plot, indexed BY PlotId — so the sim's index is the view's
  // index and neither has to look the other up.
  std::vector<aether::scene::NodeId> tiles_;
  std::vector<aether::scene::NodeId> crops_;

  // Tile transforms are written only when the OWNED COUNT moves, not every
  // frame. Untilled plots are squashed and owned ones are not, so the transform
  // depends on this one number — and rewriting 225 of them per frame to express
  // a value that changes a handful of times a session would dirty the whole
  // board's transforms for nothing. Sentinel so the first update always writes.
  aether::Usize last_unlocked_ = static_cast<aether::Usize>(-1);

  // THE ISLAND THE FARM STANDS ON. Every tile and crop hangs off this rather
  // than off the scene root, because a board is a farm's and a farm is an
  // ISLAND's: built at the root it would sit at the world origin while the
  // island it belongs to floats ninety metres away (the second farm's n5).
  aether::scene::NodeId parent_;

  // TWO crop meshes, not one: wheat and corn are told apart by silhouette
  // because the material is spent saying ripe-versus-growing.
  aether::resources::ResourceHandle<aether::resources::Mesh> wheat_mesh_;
  aether::resources::ResourceHandle<aether::resources::Mesh> corn_mesh_;
  // THREE materials for the whole board, and no more. Colour lives in the
  // material, and instancing requires an identical material id, so every
  // per-plot tint would be a draw call (the plan's §3b). Growing vs ready is
  // worth one; anything finer is not.
  aether::MaterialHandle soil_;
  aether::MaterialHandle locked_;  // land the player has not bought yet
  aether::MaterialHandle growing_;
  aether::MaterialHandle ready_;
  // Last applied, so an unchanged sky writes nothing at all.
  aether::F32 wet_ = -1.0f;
};

}  // namespace hearthfield::view
