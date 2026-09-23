// The buildings on the ground, and the ghost of the one being placed.
//
// ONE CODE PATH for every building, whether it came from the starter farm or
// from a tap five minutes ago. That is why the mill stopped being an authored
// MESH at H7 and became a placed building like any other — the authored entity
// survives as a named empty, which is the locator role GEA §16.3.3 describes
// and the only thing `MillAt()` ever needed from it.
//
// Nodes are created on demand and never destroyed: a farm gains buildings and
// does not lose them, and the save caps the table at 64.
#pragma once

#include <vector>

#include "aether/app/pbr_material_factory.hpp"
#include "aether/core/error.hpp"
#include "aether/resources/mesh.hpp"
#include "aether/resources/model.hpp"
#include "aether/resources/resource_manager.hpp"
#include "aether/scene/model_instance.hpp"
#include "aether/scene_core/scene.hpp"
#include "hf/runtime/build_ring.hpp"
#include "hf/runtime/snapshot.hpp"

namespace hearthfield::view {

class Buildings {
 public:
  [[nodiscard]] aether::Result<void> Create(
      aether::resources::ResourceManager& resources,
      aether::app::PbrMaterialFactory& materials, aether::scene::Scene& scene,
      aether::scene::NodeId parent);

  // Mirror the sim's building table onto the scene. Adds a node for anything
  // new, which is how a placement becomes visible without view/ subscribing to
  // an event — the snapshot IS the contract (AGENTS.md's sim/view split).
  // `dt` is the RENDER delta, not the fixed step: the sails turn as
  // presentation and are deliberately absent from the world state, because
  // Hearthfield's only portable oracle is a digest over that state.
  // Give every node back and forget them (sky world s5) — see Board::Clear for
  // why a stale id is worse than a missing one. The MODEL and the ghost's mesh
  // are kept: the same buildings stand on return, and dropping those handles
  // would re-import the mill on every crossing.
  void Clear(aether::scene::Scene& scene);

  void Update(aether::scene::Scene& scene, const runtime::BuildRing& ring,
              const runtime::ViewSnapshot& snapshot, aether::F32 dt);

  // Show the thing the player is dragging, at `cell`, tinted by whether it may
  // land there. `visible = false` hides it — one node, reused, because a ghost
  // that allocates on every pointer move would allocate sixty times a second.
  void ShowGhost(aether::scene::Scene& scene, const runtime::BuildRing& ring,
                 content::BuildingKind kind, runtime::BuildCell cell,
                 bool legal, bool visible);

  // Where a building's mesh stands, for app/'s audio listener. Empty until the
  // first Update.
  [[nodiscard]] aether::Vec3 CenterOf(const runtime::BuildRing& ring,
                                      runtime::BuildCell cell,
                                      content::BuildingKind kind) const;

 private:
  // One instantiated model, plus the node whose rotation is the sails. A whole
  // subtree per building rather than one mesh: the mill is four primitives
  // across three materials, and the old path drew `Meshes()[0]` — which with an
  // authored model is the tower and nothing else.
  struct Placed {
    aether::scene::ModelInstance instance;
    aether::scene::NodeId sails{};
  };

  [[nodiscard]] aether::Result<void> EnsureNode(aether::scene::Scene& scene,
                                                aether::Usize index,
                                                content::BuildingKind kind);
  [[nodiscard]] aether::Result<Placed> Instantiate(aether::scene::Scene& scene);
  void Stand(aether::scene::Scene& scene, const Placed& placed,
             aether::Vec3 centre);

  std::vector<Placed> nodes_;
  // ONE node, ONE component — the model's first primitive (the tower). See the
  // note in Create(): a node carries a MeshComponent per primitive and
  // GetComponent returns only the first, so a fuller ghost cannot be hidden.
  aether::scene::NodeId ghost_{};
  // The island this farm stands on — see view::Board for why nothing here may
  // hang off the scene root.
  aether::scene::NodeId parent_;
  aether::resources::ResourceHandle<aether::resources::Mesh> ghost_mesh_;
  aether::resources::ResourceHandle<aether::resources::Model> model_;
  aether::app::PbrMaterialFactory* materials_ = nullptr;
  aether::F32 sail_angle_ = 0.0f;
  // Two ghost materials rather than a tint: colour lives in the material and
  // the renderer keys instancing on its id, so switching between them is a
  // handle swap and never a per-frame material edit.
  aether::MaterialHandle ghost_ok_{};
  aether::MaterialHandle ghost_bad_{};
};

}  // namespace hearthfield::view
