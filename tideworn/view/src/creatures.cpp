#include "tideworn/view/creatures.hpp"

#include <cmath>
#include <utility>

#include "aether/core/math/quat.hpp"
#include "aether/scene/skinned_mesh_component.hpp"
#include "tideworn/content/content.hpp"

namespace tideworn::view {

using namespace aether;

Result<void> Creatures::Load(scene::Scene& scene,
                             resources::ResourceHandle<resources::Model> model,
                             std::span<const MaterialHandle, 3> materials,
                             std::span<const runtime::FishInstance> fish) {
  if (!model || model->Meshes().empty()) {
    return std::unexpected(
        Error(Errc::kInvalidArgument, "creatures: fish model has no mesh"));
  }
  model_ = std::move(model);
  nodes_.reserve(fish.size());
  for (const runtime::FishInstance& f : fish) {
    const scene::NodeId node = scene.CreateNode(scene.Root());
    auto* skinned = scene.AddComponent<scene::SkinnedMeshComponent>(node);
    skinned->mesh = model_->Meshes()[0].mesh;
    skinned->model = model_;
    skinned->material = materials[f.species];
    skinned->casts_shadows = false;  // nothing to receive one underwater
    skinned->BuildStates();
    if (!skinned->Snap("swim")) {
      return std::unexpected(
          Error(Errc::kNotFound, "creatures: the swim clip would not play"));
    }
    // Stagger the stroke: advance this fish's clip by its phase, and vary the
    // playback rate a little — a school in lockstep reads as a rig, not life.
    skinned->Update(f.phase);
    skinned->speed = 0.85f + 0.3f * (f.phase - std::floor(f.phase));
    nodes_.push_back(node);
  }
  return {};
}

void Creatures::Sync(scene::Scene& scene,
                     std::span<const runtime::FishInstance> fish) {
  const Usize count = std::min(nodes_.size(), fish.size());
  for (Usize i = 0; i < count; ++i) {
    const runtime::FishInstance& f = fish[i];
    const F32 s = content::kSpecies[f.species].scale;
    // The model faces +X; yaw about Y points it down its velocity, a gentle
    // pitch follows the climb/dive without ever rolling the fish.
    const F32 yaw = std::atan2(-f.vel.z, f.vel.x);
    const F32 horizontal = std::hypot(f.vel.x, f.vel.z);
    const F32 pitch = std::atan2(f.vel.y, std::max(horizontal, 0.05f));
    scene.SetLocalTransform(
        nodes_[i],
        Transform{.position = f.pos,
                  .rotation = QuatFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yaw) *
                              QuatFromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, pitch),
                  .scale = Vec3{s, s, s}});
  }
}

}  // namespace tideworn::view
