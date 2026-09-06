// The fair kinematic controller (gravity, coyote time, jump buffering, variable
// height, apex float, skid), pure over (input, WorldQuery, WeatherQuery).
#pragma once

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "cr/features/movement/components.hpp"
#include "cr/runtime/sim_system.hpp"

namespace game {

class PlayerController {
 public:
  // Clear velocity + timers (call on (re)spawn).
  void Reset();

  // Advance one fixed step from `start`: horizontal accel/friction (wet+wind+
  // skid), the buffered variable-height jump, gravity, then resolve vs solids.
  [[nodiscard]] PlayerStep Advance(const WorldQuery& world, aether::Vec2 start,
                                   const WeatherQuery& weather, aether::F32 dt,
                                   const PlayerInput& in);

  [[nodiscard]] bool OnGround() const { return on_ground_; }
  [[nodiscard]] aether::Vec2 Velocity() const { return velocity_; }

 private:
  // Advance's phases, in the order it calls them. Split for readability ONLY:
  // each holds its statements verbatim, because the byte-identical win capture
  // is this game's regression oracle and reordering float ops would move it.
  void StepHorizontal(const PlayerInput& in, aether::F32 slip, aether::F32 dt);
  void StepJumpAndGravity(const PlayerInput& in, aether::F32 dt);
  // Zeroes the velocity components the world blocked, and reports a wall hit.
  void ResolveBlocked(PlayerStep& step, aether::Vec2 fixed);

  aether::Vec2 velocity_{0.0f, 0.0f};
  bool on_ground_ = false;
  aether::F32 coyote_ = 0.0f;       // ledge grace timer
  aether::F32 jump_buffer_ = 0.0f;  // pre-land jump memory
  aether::F32 ground_wet_ = 0.0f;   // cached wetness of the tile under us
};

// The movement feature as a runtime SimSystem: advances the player over the
// shared world and emits Jumped/Landed/WallHit. `blocked` feeds camera + turn.
class MovementSystem final : public SimSystem {
 public:
  void Reset() { controller_.Reset(); }
  void Step(StepContext& ctx, const EventList& in, EventList& out) override;

  [[nodiscard]] bool OnGround() const { return controller_.OnGround(); }
  [[nodiscard]] aether::Vec2 Velocity() const { return controller_.Velocity(); }
  [[nodiscard]] aether::Vec2 LastBlocked() const { return last_blocked_; }

 private:
  PlayerController controller_;
  aether::Vec2 last_blocked_{0.0f, 0.0f};
};

}  // namespace game
