#include "cr/features/movement/system.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "aether/core/math/geometry.hpp"  // Ray2
#include "aether/scene_core/scene.hpp"
#include "cr/features/movement/tuning.hpp"
#include "cr/runtime/world_view.hpp"

namespace game {
namespace {

using namespace aether;

F32 MoveToward(F32 from, F32 to, F32 max_delta) {
  const F32 d = to - from;
  return std::abs(d) <= max_delta ? to : from + std::copysign(max_delta, d);
}

// Adapt the shared WorldView into the controller's collision seam: the game's
// solid layer, resolved against the scene.
struct WorldCollision final : WorldQuery {
  WorldCollision(scene::Scene& s, U32 m) : scene(s), mask(m) {}
  scene::Scene& scene;
  U32 mask;

  [[nodiscard]] Vec2 ResolveCircle(Vec2 center, F32 radius) const override {
    return scene.ResolveCircle(center, radius, mask);
  }
  [[nodiscard]] std::optional<GroundHit> RaycastDown(
      Vec2 origin) const override {
    const auto hit = scene.Raycast(
        Ray2{.origin = origin, .direction = Vec2{0.0f, -1.0f}}, mask);
    if (!hit) {
      return std::nullopt;
    }
    return GroundHit{.distance = hit->t,
                     .surface = static_cast<U64>(hit->node.id)};
  }
};

// Adapt WorldView's weather sources into the controller's weather seam.
struct WorldWeatherRead final : WeatherQuery {
  explicit WorldWeatherRead(const WorldView& w) : world(w) {}
  const WorldView& world;

  [[nodiscard]] Vec3 WindAt(Vec2 p) const override { return world.WindAt(p); }
  [[nodiscard]] F32 WetnessOf(U64 surface) const override {
    return world.WetnessOf(surface);
  }
};

}  // namespace

void PlayerController::Reset() {
  velocity_ = Vec2{0.0f, 0.0f};
  on_ground_ = false;
  coyote_ = 0.0f;
  jump_buffer_ = 0.0f;
  ground_wet_ = 0.0f;
}

void PlayerController::StepHorizontal(const PlayerInput& in, F32 slip, F32 dt) {
  if (std::abs(in.move) > 0.01f) {
    // Steering INTO your motion accelerates; steering AGAINST it is a skid — a
    // higher turn accel so direction changes snap without feeling slippery.
    const bool reversing = in.move * velocity_.x < 0.0f;
    F32 accel = on_ground_ ? kGroundAccel : kAirAccel;
    if (reversing) {
      accel = on_ground_ ? kGroundTurn : kAirTurn;
    }
    velocity_.x =
        MoveToward(velocity_.x, in.move * kRunSpeed, accel * slip * dt);
  } else {
    const F32 fr = (on_ground_ ? kGroundFriction : kAirFriction) * slip;
    velocity_.x = MoveToward(velocity_.x, 0.0f, fr * dt);
  }
}

void PlayerController::StepJumpAndGravity(const PlayerInput& in, F32 dt) {
  jump_buffer_ =
      in.jump_pressed ? kJumpBufferT : std::max(0.0f, jump_buffer_ - dt);
  coyote_ = on_ground_ ? kCoyote : std::max(0.0f, coyote_ - dt);
  if (jump_buffer_ > 0.0f && coyote_ > 0.0f) {
    velocity_.y = kJumpVel;
    jump_buffer_ = 0.0f;
    coyote_ = 0.0f;
    on_ground_ = false;
  }
  F32 g = kGravity;
  if (velocity_.y > 0.0f && !in.jump_held) {
    g *= kLowJumpMul;
  } else if (std::abs(velocity_.y) < kApexVel) {
    g *= kApexGravityMul;
  } else if (velocity_.y < 0.0f) {
    g *= kFallGravityMul;
  }
  velocity_.y = std::max(velocity_.y - g * dt, -kMaxFall);
}

void PlayerController::ResolveBlocked(PlayerStep& step, Vec2 fixed) {
  if (step.blocked.y > 0.5f && velocity_.y < 0.0f) {
    velocity_.y = 0.0f;  // landed
  } else if (step.blocked.y < -0.5f && velocity_.y > 0.0f) {
    velocity_.y = 0.0f;  // head bonk
  }
  if (std::abs(step.blocked.x) > 0.5f) {
    velocity_.x = 0.0f;  // ran into a wall
    const F32 s = step.blocked.x > 0.0f ? -kPlayerRadius : kPlayerRadius;
    step.wall_hit = true;
    step.wall_point = Vec2{fixed.x + s, fixed.y};
  }
}

PlayerStep PlayerController::Advance(const WorldQuery& world, Vec2 start,
                                     const WeatherQuery& weather, F32 dt,
                                     const PlayerInput& in) {
  // Wet platforms cut traction (slower to speed up AND to stop → the player
  // slides). ground_wet_ is last frame's cached ground-tile wetness.
  const F32 slip = on_ground_ ? (1.0f - ground_wet_ * kWetSlip) : 1.0f;
  StepHorizontal(in, slip, dt);
  // Wind: a lateral shove (stronger mid-air, so gusts bend jump arcs).
  const Vec3 wind = weather.WindAt(start);
  velocity_.x += wind.x * (on_ground_ ? kWindOnGround : kWindAirborne) * dt;
  StepJumpAndGravity(in, dt);

  const Vec2 want{start.x + velocity_.x * dt, start.y + velocity_.y * dt};
  const Vec2 fixed = world.ResolveCircle(want, kPlayerRadius);
  PlayerStep step;
  step.blocked = Vec2{fixed.x - want.x, fixed.y - want.y};
  ResolveBlocked(step, fixed);
  step.position = fixed;

  const std::optional<GroundHit> below = world.RaycastDown(fixed);
  const bool was_ground = on_ground_;
  on_ground_ = below.has_value() &&
               below->distance <= kPlayerRadius + kGroundSnap &&
               velocity_.y <= 1.0f;
  step.landed = on_ground_ && !was_ground;
  // Cache the wetness of the tile we're standing on for next frame's traction.
  ground_wet_ = (on_ground_ && below.has_value())
                    ? weather.WetnessOf(below->surface)
                    : 0.0f;
  return step;
}

void MovementSystem::Step(StepContext& ctx, const EventList& /*in*/,
                          EventList& out) {
  WorldView& world = ctx.world;
  scene::Node* p = world.Scene().Get(world.Player());
  if (p == nullptr) {
    return;
  }
  const WorldCollision collision{world.Scene(), world.SolidMask()};
  const WorldWeatherRead weather{world};
  const PlayerInput in{.move = ctx.input.move,
                       .jump_pressed = ctx.input.jump_pressed,
                       .jump_held = ctx.input.jump_held};
  const PlayerStep step = controller_.Advance(collision, p->local.position.xy(),
                                              weather, ctx.dt, in);
  p->local.position.x = step.position.x;
  p->local.position.y = step.position.y;
  last_blocked_ = step.blocked;
  // Reactions become events (view drains them); the controller stays pure.
  if (step.landed) {
    out.push_back(Landed{
        .point = Vec2{step.position.x, step.position.y - kPlayerRadius}});
  }
  if (step.wall_hit) {
    out.push_back(WallHit{.point = step.wall_point});
  }
}

}  // namespace game
