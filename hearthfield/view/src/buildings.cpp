#include "hf/view/buildings.hpp"

#include <cmath>
#include <numbers>
#include <string>

#include "aether/core/math/transform.hpp"
#include "aether/resources/material.hpp"
#include "aether/resources/model.hpp"
#include "aether/scene/mesh_component.hpp"
#include "aether/scene/model_instance.hpp"
#include "hf/content/buildings.hpp"

namespace hearthfield::view {
namespace {

using namespace aether;

// How fast the sails turn. Slow: a mill is 3.2 m tall on a 12 m view, so at any
// speed that reads as "working machinery" up close the tips smear at this size.
constexpr F32 kSailRadiansPerSecond = 0.9f;

// The node the model authors for the turning sails. model_studio puts the RAKE
// on its parent, because writing this node's local rotation overwrites whatever
// was there — rake here would be erased on the first frame.
constexpr std::string_view kSailNode = "mill_sails";

[[nodiscard]] Result<MaterialHandle> MakeMaterial(
    app::PbrMaterialFactory& materials, const char* name, Vec4 rgba,
    F32 roughness) {
  const resources::Material material(
      name,
      resources::PbrParams{
          .base_color = rgba, .metallic = 0.0f, .roughness = roughness},
      {}, rhi::RenderState{});
  return materials.Create(material);
}

}  // namespace

Result<void> Buildings::Create(resources::ResourceManager& resources,
                               app::PbrMaterialFactory& materials,
                               scene::Scene& scene, scene::NodeId parent) {
  parent_ = parent;
  // EVERY KIND SHARES ONE MODEL today, because only one kind exists. The
  // catalogue names a model per kind so that stops being true without a code
  // change; loading the first kind's is what this does until a second
  // disagrees.
  auto model = resources.Load<resources::Model>(
      std::string(content::BuildingTypeOf(content::kMillKind).model));
  if (!model) {
    return std::unexpected(model.error());
  }
  if ((*model)->Meshes().empty()) {
    return Fail(Errc::kInvalidArgument, "the building model has no mesh");
  }
  model_ = *model;
  materials_ = &materials;

  auto ok = MakeMaterial(materials, "hf:ghost_ok",
                         Vec4{0.35f, 0.85f, 0.40f, 1.0f}, 0.5f);
  if (!ok) {
    return std::unexpected(ok.error());
  }
  auto bad = MakeMaterial(materials, "hf:ghost_bad",
                          Vec4{0.90f, 0.30f, 0.26f, 1.0f}, 0.5f);
  if (!bad) {
    return std::unexpected(bad.error());
  }
  ghost_ok_ = *ok;
  ghost_bad_ = *bad;

  // THE GHOST IS THE TOWER, not the whole model, and that is a limitation
  // rather than a choice. `InstantiateModel` attaches one MeshComponent PER
  // PRIMITIVE to the same node, and `Scene::GetComponent` returns only the
  // first — so a full-fidelity ghost could be neither hidden nor tinted nor
  // stopped from casting past its first primitive. The first attempt did
  // exactly that and left a red roof-cone floating over the board with no
  // tower under it. The tower carries the mass and the footprint, which is
  // what a placement preview is for; the rest needs an engine change.
  ghost_ = scene.CreateNode(parent_);
  ghost_mesh_ = model_->Meshes()[0].mesh;
  auto* component = scene.AddComponent<scene::MeshComponent>(ghost_);
  component->material = ghost_ok_;
  // A ghost casts no shadow: it is not there yet, and a shadow under a preview
  // reads as a building that has already been placed.
  component->casts_shadows = false;
  component->mesh = {};  // extracts nothing until a placement starts
  return {};
}

Result<Buildings::Placed> Buildings::Instantiate(scene::Scene& scene) {
  auto instance = scene::InstantiateModel(scene, parent_, model_, *materials_);
  if (!instance) {
    return std::unexpected(instance.error());
  }
  Placed placed{.instance = *instance,
                .sails = instance->Find(*model_, kSailNode)};
  return placed;
}

Result<void> Buildings::EnsureNode(scene::Scene& scene, Usize index,
                                   content::BuildingKind /*kind*/) {
  while (nodes_.size() <= index) {
    auto placed = Instantiate(scene);
    if (!placed) {
      return std::unexpected(placed.error());
    }
    nodes_.push_back(*placed);
  }
  return {};
}

Vec3 Buildings::CenterOf(const runtime::BuildRing& ring,
                         runtime::BuildCell cell,
                         content::BuildingKind kind) const {
  const U32 span = content::BuildingKindExists(kind)
                       ? content::BuildingTypeOf(kind).span
                       : 1;
  return ring.CenterOfSpan(cell, span);
}

void Buildings::Stand(scene::Scene& scene, const Placed& placed, Vec3 centre) {
  // NO SCALE. The model is authored at true metric size with its pivot at the
  // base on the tower axis (ADR-0135), so it stands where it is put. The old
  // path scaled a unit cube to the cell, which is the pivot fork the art
  // pipeline plan warned about: apply both and the mill is wrong by its height.
  scene.SetLocalTransform(placed.instance.root,
                          Transform{.position = Vec3{centre.x, 0.0f, centre.z},
                                    .rotation = Quat::Identity(),
                                    .scale = Vec3{1.0f, 1.0f, 1.0f}});
  if (placed.sails != scene::NodeId{}) {
    scene.SetLocalTransform(placed.sails,
                            Transform{.position = Vec3{0.0f, 0.0f, 0.0f},
                                      .rotation = QuatFromAxisAngle(
                                          Vec3{0.0f, 0.0f, 1.0f}, sail_angle_),
                                      .scale = Vec3{1.0f, 1.0f, 1.0f}});
  }
}

void Buildings::Clear(scene::Scene& scene) {
  // One DestroyNode per building, because each is a whole ModelInstance subtree
  // hanging off its own root — which is what that wrapper is for.
  for (const Placed& placed : nodes_) {
    scene.DestroyNode(placed.instance.root);
  }
  nodes_.clear();
  if (ghost_.Valid()) {
    scene.DestroyNode(ghost_);
    ghost_ = scene::NodeId{};
  }
}

void Buildings::Update(scene::Scene& scene, const runtime::BuildRing& ring,
                       const runtime::ViewSnapshot& snapshot, F32 dt) {
  // THE RENDER dt, and the sails are NOT in the world state — the same rule
  // weather follows. A turning sail is presentation: put it in the sim and it
  // enters the world hash, and Hearthfield's only portable oracle is a digest
  // over that hash.
  sail_angle_ += kSailRadiansPerSecond * dt;
  constexpr F32 kTau = 2.0f * std::numbers::pi_v<F32>;
  sail_angle_ = std::fmod(sail_angle_, kTau);

  for (Usize i = 0; i < snapshot.buildings.size(); ++i) {
    const runtime::BuildingView& view = snapshot.buildings[i];
    if (!content::BuildingKindExists(view.kind)) {
      continue;  // a save from a build with more kinds than this one has
    }
    (void)EnsureNode(scene, i, view.kind);
    const content::BuildingType& type = content::BuildingTypeOf(view.kind);
    Stand(scene, nodes_[i], ring.CenterOfSpan(view.cell, type.span));
  }
}

void Buildings::ShowGhost(scene::Scene& scene, const runtime::BuildRing& ring,
                          content::BuildingKind kind, runtime::BuildCell cell,
                          bool legal, bool visible) {
  auto* component = scene.GetComponent<scene::MeshComponent>(ghost_);
  if (component == nullptr) {
    return;
  }
  if (!visible || !content::BuildingKindExists(kind)) {
    component->mesh = {};  // extracts nothing
    return;
  }
  component->mesh = ghost_mesh_;
  component->material = legal ? ghost_ok_ : ghost_bad_;
  const content::BuildingType& type = content::BuildingTypeOf(kind);
  const Vec3 centre = ring.CenterOfSpan(cell, type.span);
  // Metric model, so no scale — the same rule `Stand` follows.
  scene.SetLocalTransform(ghost_,
                          Transform{.position = Vec3{centre.x, 0.0f, centre.z},
                                    .rotation = Quat::Identity(),
                                    .scale = Vec3{1.0f, 1.0f, 1.0f}});
}

}  // namespace hearthfield::view
