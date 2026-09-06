#include "cr/features/progression/system.hpp"

#include <algorithm>
#include <variant>

#include "aether/core/math/math.hpp"  // Length
#include "aether/scene_core/scene.hpp"
#include "cr/runtime/world_view.hpp"

namespace game {

using namespace aether;

void ProgressSystem::Step(StepContext& ctx, const EventList& in,
                          EventList& out) {
  RoundProgress& pr = ctx.world.Progress();
  if (pr.round_over) {
    return;
  }
  pr.time_left = std::max(0.0f, pr.time_left - ctx.dt);  // soft timer (stars)
  for (const GameEvent& e : in) {  // this step's pickups (coins ran before us)
    if (std::holds_alternative<CoinCollected>(e)) {
      ++pr.collected;
    }
  }
  if (!pr.exit_unlocked && pr.total > 0 && pr.collected >= pr.total) {
    pr.exit_unlocked = true;
    out.push_back(ExitUnlocked{});
  }
  if (pr.exit_unlocked) {
    const scene::Node* p = ctx.world.Scene().Get(ctx.world.Player());
    if (p != nullptr &&
        Length(pr.exit_pos - p->local.position.xy()) < pr.win_radius) {
      pr.round_over = true;
      out.push_back(RoundWon{.stars = 0});  // real stars set in the reaction
    }
  }
}

}  // namespace game
