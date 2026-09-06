#include "hf/view/board.hpp"

#include <algorithm>
#include <utility>

#include "aether/core/math/transform.hpp"
#include "aether/resources/material.hpp"
#include "aether/resources/model.hpp"
#include "aether/resources/texture.hpp"
#include "aether/scene/mesh_component.hpp"
#include "hf/content/farm.hpp"

namespace hearthfield::view {
namespace {

using namespace aether;

[[nodiscard]] Result<MaterialHandle> MakeMaterial(
    app::PbrMaterialFactory& materials, const char* name, Vec3 rgb,
    F32 roughness, resources::MaterialSlots slots = {}) {
  const resources::Material material(
      name,
      resources::PbrParams{.base_color = Vec4{rgb.x, rgb.y, rgb.z, 1.0f},
                           .metallic = 0.0f,
                           .roughness = roughness},
      std::move(slots), rhi::RenderState{});
  return materials.Create(material);
}

}  // namespace

Result<void> Board::Create(resources::ResourceManager& resources,
                           app::PbrMaterialFactory& materials,
                           scene::Scene& scene, const runtime::Grid& grid,
                           scene::NodeId parent) {
  parent_ = parent;
  auto tile_model =
      resources.Load<resources::Model>(std::string(content::kTileModel));
  if (!tile_model) {
    return std::unexpected(tile_model.error());
  }
  auto wheat_model =
      resources.Load<resources::Model>(std::string(content::kWheatModel));
  if (!wheat_model) {
    return std::unexpected(wheat_model.error());
  }
  auto corn_model =
      resources.Load<resources::Model>(std::string(content::kCornModel));
  if (!corn_model) {
    return std::unexpected(corn_model.error());
  }
  if ((*tile_model)->Meshes().empty() || (*wheat_model)->Meshes().empty() ||
      (*corn_model)->Meshes().empty()) {
    return Fail(Errc::kInvalidArgument, "a board primitive has no mesh");
  }
  const auto tile_mesh = (*tile_model)->Meshes()[0].mesh;
  wheat_mesh_ = (*wheat_model)->Meshes()[0].mesh;
  corn_mesh_ = (*corn_model)->Meshes()[0].mesh;

  // The furrowed soil surface. WHITE base colour, because glTF multiplies the
  // factor by the texture and the tile's reflectance is already calibrated into
  // the image (pixel_studio --albedo-range) — tinting it here would darken the
  // plots twice. Rain still darkens them at runtime through wetness.
  auto soil_texture =
      resources.Load<resources::Texture>(std::string(content::kSoilTexture));
  if (!soil_texture) {
    return std::unexpected(soil_texture.error());
  }
  resources::MaterialSlots soil_slots;
  soil_slots.albedo = *soil_texture;
  auto soil = MakeMaterial(materials, "hf:soil", Vec3{1.0f, 1.0f, 1.0f}, 0.95f,
                           std::move(soil_slots));
  if (!soil) {
    return std::unexpected(soil.error());
  }
  auto growing = MakeMaterial(materials, "hf:crop_growing",
                              Vec3{0.35f, 0.62f, 0.22f}, 0.75f);
  if (!growing) {
    return std::unexpected(growing.error());
  }
  // Ripe wheat, and unmistakably a different colour from growing: "is this one
  // ready" must be answerable at a glance across the whole board, which is the
  // genre's core read (spec §1.1).
  auto ready =
      MakeMaterial(materials, "hf:crop_ready", Vec3{0.88f, 0.74f, 0.24f}, 0.6f);
  if (!ready) {
    return std::unexpected(ready.error());
  }
  // Unbought land is BARE UNWORKED EARTH, and since 2026-08-28 it is textured
  // rather than a flat colour. THIS IS THE MAJORITY SURFACE, not the exception:
  // a fresh farm owns 4 plots of 576, so `hf:locked` is 99% of what the board
  // looks like and `hf:soil` is the rare case. It had no art made for it at all
  // while the 1%-case did, and the board read as a faint lattice because of it.
  //
  // The difference from `hf:soil` is STRUCTURE, not hue: furrows mean worked,
  // so this is clods and loose stones with no direction, and buying a plot
  // turns rough ground into rows. Calibrated 0.08-0.24 linear against soil's
  // 0.10-0.28, so a bought plot is visibly the richer one — a locked plot that
  // outshone a bought one would invert the reward.
  //
  // WHITE factor, for the same reason `hf:soil` uses one: glTF multiplies the
  // factor by the texture and the reflectance is already calibrated into the
  // image, so tinting here would darken it twice.
  //
  // Still a FOURTH material rather than a tint — colour lives in the material
  // and instancing keys on its id, so the cost of telling owned from unowned is
  // exactly one more draw whatever the board's size.
  auto earth_texture =
      resources.Load<resources::Texture>(std::string(content::kLockedTexture));
  if (!earth_texture) {
    return std::unexpected(earth_texture.error());
  }
  resources::MaterialSlots earth_slots;
  earth_slots.albedo = *earth_texture;
  auto locked = MakeMaterial(materials, "hf:locked", Vec3{1.0f, 1.0f, 1.0f},
                             0.98f, std::move(earth_slots));
  if (!locked) {
    return std::unexpected(locked.error());
  }
  soil_ = *soil;
  locked_ = *locked;
  growing_ = *growing;
  ready_ = *ready;

  const Usize count = grid.Count();
  tiles_.reserve(count);
  crops_.reserve(count);
  for (Usize i = 0; i < count; ++i) {
    const Vec3 centre = grid.CenterOf(static_cast<runtime::PlotId>(i));

    const scene::NodeId tile = scene.CreateNode(parent_);
    auto* tile_mesh_component = scene.AddComponent<scene::MeshComponent>(tile);
    tile_mesh_component->mesh = tile_mesh;
    tile_mesh_component->material = soil_;
    // A soil tile casts nothing: it is flush with the ground it sits on, so its
    // shadow is invisible and every one of them would still cost a caster.
    tile_mesh_component->casts_shadows = false;
    // Scale 1 and no half-height lift: tile.glb carries its own metres and its
    // own base pivot (ADR-0135). The mesh's width is (kCellSize - kTileGap) by
    // authorship, so a grid whose cell is not kCellSize would need the scale
    // back.
    scene.SetLocalTransform(
        tile, Transform{.position = Vec3{centre.x, 0.0f, centre.z},
                        .rotation = Quat::Identity(),
                        .scale = Vec3{1.0f, 1.0f, 1.0f}});
    tiles_.push_back(tile);

    // The crop node exists from the start and is EMPTIED rather than destroyed:
    // a MeshComponent with no mesh extracts nothing, so an unsown plot costs no
    // draw call and sowing costs no allocation.
    const scene::NodeId crop = scene.CreateNode(parent_);
    auto* crop_mesh_component = scene.AddComponent<scene::MeshComponent>(crop);
    crop_mesh_component->material = growing_;
    crops_.push_back(crop);
  }
  return {};
}

void Board::Clear(scene::Scene& scene) {
  // Tiles and crops are separate nodes under the scene root rather than under
  // one wrapper, so this is a loop and not a DestroyNode. Both vectors are
  // emptied: a stale id is worse than a missing one, because Update indexes
  // these BY PlotId and would write into whatever the scene put there next.
  for (const scene::NodeId id : tiles_) {
    scene.DestroyNode(id);
  }
  for (const scene::NodeId id : crops_) {
    scene.DestroyNode(id);
  }
  tiles_.clear();
  crops_.clear();
  // The sentinel, so a rebuilt board writes its tile transforms on the first
  // update instead of believing the owned count has not moved since.
  last_unlocked_ = static_cast<Usize>(-1);
}

void Board::Update(scene::Scene& scene, const runtime::Grid& grid,
                   const runtime::ViewSnapshot& snapshot) {
  const Usize count = std::min(crops_.size(), snapshot.plots.size());
  const bool unlocked_moved = snapshot.unlocked != last_unlocked_;
  last_unlocked_ = snapshot.unlocked;
  for (Usize i = 0; i < count; ++i) {
    const runtime::PlotView& plot = snapshot.plots[i];
    const bool owned = i < snapshot.unlocked;
    if (auto* tile = scene.GetComponent<scene::MeshComponent>(tiles_[i])) {
      tile->material = owned ? soil_ : locked_;
    }
    // Untilled land is also FLATTER: a full-height slab in grass green still
    // reads as a raised block. Squashed to a fifth it keeps an edge — enough
    // for the grid and the buy affordance to stay legible — without pretending
    // the ground has been worked. Base-pivoted (ADR-0135), so this only lowers
    // the top face and the tile stays seated.
    if (unlocked_moved) {
      const Vec3 centre = grid.CenterOf(static_cast<runtime::PlotId>(i));
      scene.SetLocalTransform(
          tiles_[i], Transform{.position = Vec3{centre.x, 0.0f, centre.z},
                               .rotation = Quat::Identity(),
                               .scale = Vec3{1.0f, owned ? 1.0f : 0.2f, 1.0f}});
    }
    auto* mesh = scene.GetComponent<scene::MeshComponent>(crops_[i]);
    if (mesh == nullptr) {
      continue;
    }
    if (plot.state == runtime::PlotState::kEmpty) {
      mesh->mesh = {};
      continue;
    }
    // SILHOUETTE says which crop, MATERIAL says whether it is ready. Colour is
    // spent entirely on the second, because "is this one ready" must be
    // answerable at a glance across 576 plots and that is the genre's core
    // read.
    mesh->mesh = plot.crop == content::kCorn ? corn_mesh_ : wheat_mesh_;
    mesh->material =
        plot.state == runtime::PlotState::kReady ? ready_ : growing_;

    // GROWTH IS THE TRANSFORM. Per-instance, so two hundred crops at two
    // hundred different stages are still one batch — the whole reason a growth
    // stage is not a separate mesh.
    // GROWTH IS THE ONLY SCALE NOW. The cone these replaced was a UNIT shape
    // sized here by kCropRadius/kCropHeight and lifted by half its height
    // because it was centred; wheat and corn carry their own metres and sit on
    // their own base (ADR-0135), so this places them on the tile's top face and
    // multiplies by growth. Applying both would be wrong by the crop's height.
    const F32 grown = std::max(content::kMinGrowth, plot.growth);
    const Vec3 centre = grid.CenterOf(static_cast<runtime::PlotId>(i));
    scene.SetLocalTransform(
        crops_[i],
        Transform{.position = Vec3{centre.x, content::kTileHeight, centre.z},
                  .rotation = Quat::Identity(),
                  .scale = Vec3{grown, grown, grown}});
  }
}

void Board::SetWetness(scene::Scene& scene, F32 wetness) {
  // THE TILES ONLY. A crop is a plant, not a surface: wetting the cones as well
  // would darken the one thing on this board whose colour carries meaning —
  // ready versus growing is the genre's core read.
  if (wetness == wet_) {
    return;
  }
  wet_ = wetness;
  for (const scene::NodeId tile : tiles_) {
    if (auto* mesh = scene.GetComponent<scene::MeshComponent>(tile)) {
      mesh->wetness = wetness;
    }
  }
}

}  // namespace hearthfield::view
