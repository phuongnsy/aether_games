#include "hf/view/steading.hpp"

#include <cmath>
#include <numbers>
#include <string>

#include "aether/core/log.hpp"
#include "aether/core/math/transform.hpp"
#include "aether/resources/material.hpp"
#include "hf/content/animals.hpp"
#include "hf/content/farm.hpp"

namespace hearthfield::view {
namespace {

using namespace aether;

// The world position of an authored entity, and whether it was there at all.
struct Placed {
  Vec3 at{0.0f, 0.0f, 0.0f};
  bool found = false;
};

[[nodiscard]] Placed Locate(scene::Scene& scene,
                            const resources::WorldChunk& chunk,
                            const scene::WorldInstance& instance,
                            std::string_view name) {
  const scene::NodeId node = scene::FindEntityNode(chunk, instance, name);
  if (!node.Valid()) {
    return Placed{};
  }
  // From the node's WORLD transform, not from the authored "pos": an entity may
  // be parented or the chunk re-rooted, and the sound has to come from where
  // the thing actually is.
  const scene::Node* found = scene.Get(node);
  if (found == nullptr) {
    return Placed{};
  }
  return Placed{.at = TransformPoint(found->world, Vec3{}), .found = true};
}

}  // namespace

Result<void> Steading::Create(resources::ResourceManager& resources,
                              app::PbrMaterialFactory& materials,
                              scene::Scene& scene,
                              const resources::WorldChunk& chunk,
                              const scene::WorldInstance& instance,
                              scene::NodeId parent) {
  parent_ = parent;
  // SyncTransforms, NOT Update. `Scene::Update` ticks components and does not
  // touch `Node::world` at all — world matrices are propagated by
  // SyncTransforms, which BuildRenderFrame calls for itself. A locator read
  // before that is at the ORIGIN with an identity basis: found, plausible, and
  // wrong, so every sound on the farm came from the middle of the board and
  // four hens stood in the crops.
  scene.SyncTransforms();

  const Placed mill = Locate(scene, chunk, instance, content::kMillEntity);
  const Placed coop = Locate(scene, chunk, instance, content::kCoopEntity);
  const Placed yard = Locate(scene, chunk, instance, content::kYardEntity);
  mill_at_ = mill.at;
  coop_at_ = coop.at;
  has_mill_ = mill.found;
  // LOUD, per the risk in the world-entity-names plan: a silent miss puts the
  // mill's rumble at the origin, which sounds like a mixing bug rather than
  // like a farm whose world file lost an entity.
  if (!mill.found) {
    LogWarn("hearthfield: the world names no '{}' — the mill will be silent",
            content::kMillEntity);
  }
  if (!yard.found) {
    LogWarn("hearthfield: the world names no '{}' — the flock has nowhere",
            content::kYardEntity);
    return {};
  }

  const resources::Material feather_asset(
      "hf:feather",
      resources::PbrParams{.base_color = Vec4{0.90f, 0.86f, 0.76f, 1.0f},
                           .metallic = 0.0f,
                           .roughness = 0.72f},
      {}, rhi::RenderState{});
  auto feather = materials.Create(feather_asset);
  if (!feather) {
    return std::unexpected(feather.error());
  }

  // A missing model costs the birds, not the game — the same fail-soft bargain
  // the font and the audio clips already make.
  auto flock_model =
      resources.Load<resources::Model>(std::string(content::kChickenModel));
  if (!flock_model) {
    LogWarn("hearthfield: no chicken model ({}) — the coop will be empty",
            flock_model.error().message);
    return {};
  }
  if ((*flock_model)->Meshes().empty()) {
    return Fail(Errc::kInvalidArgument, "the chicken model has no mesh");
  }

  // Where the island itself stands, so a world-space locator can be expressed
  // in the parent-relative terms a node transform actually takes.
  Vec3 island{0.0f, 0.0f, 0.0f};
  if (const scene::Node* anchor = scene.Get(parent_); anchor != nullptr) {
    island = TransformPoint(anchor->world, Vec3{});
  }

  birds_.reserve(content::kFlockSize);
  for (U8 i = 0; i < content::kFlockSize; ++i) {
    const F32 turn = (2.0f * std::numbers::pi_v<F32> * static_cast<F32>(i)) /
                     static_cast<F32>(content::kFlockSize);
    // MINUS THE ISLAND'S OWN POSITION, because `yard.at` is a WORLD point and
    // this node's transform is read relative to its parent. On the hub that
    // subtraction is zero and it looked correct for as long as there was only
    // one island; on a satellite ninety metres out it is the difference between
    // birds in the yard and birds in open sky. Latent rather than observed —
    // the satellite has no yard locator yet, so this returns early there — and
    // fixed now because the next zone to gain one would have found it the hard
    // way.
    const Vec3 at = yard.at - island +
                    Vec3{std::cos(turn) * content::kYardRadius, 0.0f,
                         std::sin(turn) * content::kYardRadius};
    const scene::NodeId node = scene.CreateNode(parent_);
    auto* skinned = scene.AddComponent<scene::SkinnedMeshComponent>(node);
    skinned->mesh = (*flock_model)->Meshes()[0].mesh;
    skinned->model = *flock_model;
    skinned->material = *feather;
    skinned->BuildStates();
    // NO LODs. The chain exists on SkinnedMeshComponent and four birds do not
    // want it — ADR-0107 already measured that this board's static meshes do
    // not either. Stated so the absence reads as a decision.
    if (!skinned->Snap(std::string(content::kIdleClip))) {
      return Fail(Errc::kNotFound, "the chicken has no idle clip");
    }
    // Each bird faces out of the ring, and each runs its clip at a different
    // RATE — four hens nodding in perfect unison read as one hen drawn four
    // times, which is the thing an instanced crowd always gets wrong.
    skinned->speed = 0.85f + (0.1f * static_cast<F32>(i));
    scene.SetLocalTransform(
        node,
        Transform{.position = at,
                  .rotation = QuatFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, turn),
                  .scale = Vec3{content::kChickenScale, content::kChickenScale,
                                content::kChickenScale}});
    birds_.push_back(skinned);
  }
  return {};
}

void Steading::Clear() {
  // Pointers only. The birds' NODES belong to the island's chunk and go with it
  // when travel destroys that; what would survive is this vector of pointers
  // into freed component storage, and Update dereferences every one of them.
  birds_.clear();
  has_mill_ = false;
  mill_at_ = Vec3{};
  coop_at_ = Vec3{};
  // So a rebuilt flock is told which clip to play rather than being assumed to
  // already be in the state the last farm left it in.
  pecking_ = false;
}

void Steading::Update(const runtime::ViewSnapshot& snapshot) {
  // ONE clip change on a transition, not a Play() every frame: Play
  // cross-fades, and re-requesting the state you are already in restarts that
  // fade forever.
  if (snapshot.coop_fed == pecking_) {
    return;
  }
  pecking_ = snapshot.coop_fed;
  const std::string clip(pecking_ ? content::kPeckClip : content::kIdleClip);
  for (scene::SkinnedMeshComponent* bird : birds_) {
    (void)bird->Play(clip);
  }
}

}  // namespace hearthfield::view
