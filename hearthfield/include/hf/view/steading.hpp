// The flock, and where the farm's sounds come from.
//
// SINCE H6 THIS BUILDS ALMOST NOTHING. The mill, the coop and the yard are
// authored in `farm.world.json` and spawned by the engine's own registry
// (spec check 3); what is left here is the one thing that cannot be authored —
// the birds, whose COUNT is `content::kFlockSize` and therefore the sim's, the
// same line H1 drew for the plot table.
//
// It also answers "where is the mill", because something has to and app/ may
// not go rummaging through the scene for it. The positions come from the
// authored entities, found by name (GEA §16.3.3.1's UniqueId).
#pragma once

#include <vector>

#include "aether/app/pbr_material_factory.hpp"
#include "aether/core/error.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/resources/model.hpp"
#include "aether/resources/resource_manager.hpp"
#include "aether/resources/world_chunk.hpp"
#include "aether/scene/skinned_mesh_component.hpp"
#include "aether/scene/world_instance.hpp"
#include "aether/scene_core/scene.hpp"
#include "hf/runtime/snapshot.hpp"

namespace hearthfield::view {

class Steading {
 public:
  // Reads the authored mill, coop and yard out of `chunk`/`instance`, then
  // places the flock around the yard. A missing locator costs that feature and
  // says so — the alternative is a sound at the origin, which reads as a mixing
  // bug rather than as a farm with no mill in it.
  [[nodiscard]] aether::Result<void> Create(
      aether::resources::ResourceManager& resources,
      aether::app::PbrMaterialFactory& materials, aether::scene::Scene& scene,
      const aether::resources::WorldChunk& chunk,
      const aether::scene::WorldInstance& instance,
      aether::scene::NodeId parent);

  // Forget the flock (sky world s5). THE SHARPEST CASE OF THE THREE: `birds_`
  // holds raw component POINTERS into the scene, not node ids, so once travel
  // destroys the island the farm stood on they are dangling and the next
  // Update writes through them. Nothing to destroy here — the birds' nodes go
  // with the chunk — so this only drops the pointers, which is exactly the part
  // that cannot be left undone.
  void Clear();

  // Mirror the snapshot: the birds peck when the trough has feed in it and
  // stand about when it does not. One clip change, and it is the only thing on
  // this farm that shows a sim state through MOTION rather than through colour.
  void Update(const runtime::ViewSnapshot& snapshot);

  // Where the sound comes from. app/ needs these for the listener maths.
  [[nodiscard]] aether::Vec3 MillAt() const { return mill_at_; }
  [[nodiscard]] aether::Vec3 CoopAt() const { return coop_at_; }
  [[nodiscard]] bool HasMill() const { return has_mill_; }

 private:
  std::vector<aether::scene::SkinnedMeshComponent*> birds_;
  // The island this farm stands on — see view::Board for why nothing here may
  // hang off the scene root.
  aether::scene::NodeId parent_;
  aether::Vec3 mill_at_{0.0f, 0.0f, 0.0f};
  aether::Vec3 coop_at_{0.0f, 0.0f, 0.0f};
  bool has_mill_ = false;
  bool pecking_ = false;
};

}  // namespace hearthfield::view
