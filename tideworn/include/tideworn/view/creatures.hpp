// Marine life presentation: one scene node + skinned fish per FishInstance,
// swim clips phase-staggered, transforms synced from the snapshot each frame
// (view consumes the snapshot ONLY — AGENTS.md law 2).
#pragma once

#include <span>
#include <vector>

#include "aether/resources/model.hpp"
#include "aether/scene_core/scene.hpp"
#include "tideworn/runtime/snapshot.hpp"

namespace tideworn::view {

class Creatures {
 public:
  // Builds one node per fish from the FIRST snapshot (counts are fixed at
  // spawn). `materials` is one handle per content species.
  aether::Result<void> Load(
      aether::scene::Scene& scene,
      aether::resources::ResourceHandle<aether::resources::Model> model,
      std::span<const aether::MaterialHandle, 3> materials,
      std::span<const runtime::FishInstance> fish);

  void Sync(aether::scene::Scene& scene,
            std::span<const runtime::FishInstance> fish);

 private:
  aether::resources::ResourceHandle<aether::resources::Model> model_;
  std::vector<aether::scene::NodeId> nodes_;
};

}  // namespace tideworn::view
