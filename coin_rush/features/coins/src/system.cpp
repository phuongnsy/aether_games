#include "cr/features/coins/system.hpp"

#include "aether/scene_core/scene.hpp"
#include "cr/features/coins/tuning.hpp"
#include "cr/runtime/world_view.hpp"

namespace game {

using namespace aether;

void CoinsSystem::Step(StepContext& ctx, const EventList& /*in*/,
                       EventList& out) {
  WorldView& world = ctx.world;
  const scene::Node* p = world.Scene().Get(world.Player());
  if (p == nullptr) {
    return;
  }
  const Vec2 center = p->local.position.xy();
  // OverlapCircle returns a snapshot, so removing coins mid-iteration is safe.
  for (const scene::NodeId coin :
       world.Scene().OverlapCircle(center, kPickupRadius, world.CoinMask())) {
    const scene::Node* node = world.Scene().Get(coin);
    if (node == nullptr) {
      continue;
    }
    const Vec2 pos = node->local.position.xy();
    world.Scene().DestroyNode(coin);
    out.push_back(CoinCollected{.point = pos});
  }
}

}  // namespace game
