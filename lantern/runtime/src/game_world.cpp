#include "lantern/runtime/game_world.hpp"

#include <algorithm>

#include "aether/scene_core/scene.hpp"
#include "lantern/content/spire.hpp"

namespace lantern::runtime {

void GameWorld::Reset(aether::Vec3 start) {
  start_ = start;
  walker_.Reset(start);
  events_.clear();
  was_grounded_ = false;
  fall_speed_ = 0.0f;
}

void GameWorld::Step(aether::scene::Scene& scene, const Input& input, F32 dt) {
  events_.clear();

  if (input.jump) {
    walker_.Jump();
  }
  const F32 before_y = walker_.Position().y;
  walker_.Step(scene, input.move, dt);

  // Landing, measured from the step rather than from the controller's velocity:
  // the controller zeroes vertical speed on contact, so by the time anyone can
  // ask, the impact is gone.
  const F32 dy = (walker_.Position().y - before_y) / std::max(dt, 1e-6f);
  if (walker_.Grounded() && !was_grounded_) {
    events_.push_back(Landed{.impact = -std::min(fall_speed_, 0.0f)});
  }
  fall_speed_ = walker_.Grounded() ? 0.0f : dy;
  was_grounded_ = walker_.Grounded();

  if (input.interact) {
    const aether::Usize near = lanterns_.NearestUnlit(
        walker_.Position(), content::kLightRadius, content::kLightReach);
    if (lanterns_.Light(near)) {
      events_.push_back(LanternLit{
          .index = near, .position = lanterns_.Items()[near].position});
    }
  }

  // FALLING IS THE COST, and it is checked last so a lantern lit on the way
  // down still counts — the player did reach it.
  if (walker_.Position().y < content::kFallY) {
    events_.push_back(Fell{.from = walker_.Position()});
    const bool to_checkpoint = lanterns_.HasCheckpoint();
    // Above the lantern, not at it: a checkpoint hangs over its platform, and
    // spawning inside the light fixture would drop the walker through the
    // world.
    const aether::Vec3 at =
        to_checkpoint ? lanterns_.Checkpoint() + aether::Vec3{0.0f, 0.6f, 0.0f}
                      : start_;
    walker_.Reset(at);
    events_.push_back(Respawned{.at = at, .at_checkpoint = to_checkpoint});
    was_grounded_ = false;
    fall_speed_ = 0.0f;
  }

  // Weather last: it reads the world the walker just changed nothing about,
  // and its wetness/snow must be fresh when the view reads them post-step.
  weather_.Step(dt);
}

ViewSnapshot GameWorld::Snapshot() const {
  ViewSnapshot out;
  out.character = walker_.Position();
  out.facing = walker_.Facing();
  out.speed = walker_.Speed();
  out.grounded = walker_.Grounded();
  out.lit = lanterns_.LitCount();
  out.has_checkpoint = lanterns_.HasCheckpoint();
  out.height = walker_.Position().y;
  out.lanterns.reserve(lanterns_.Count());
  for (const lanterns::Lantern& item : lanterns_.Items()) {
    out.lanterns.push_back(
        LanternView{.position = item.position, .lit = item.lit});
  }
  return out;
}

}  // namespace lantern::runtime
