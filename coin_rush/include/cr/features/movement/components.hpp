// Movement feature — plain data + the narrow query seams the system reads. Pure
// given (PlayerInput, WorldQuery, WeatherQuery), so tests supply trivial fakes.
#pragma once

#include <optional>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace game {

// The player's collision-circle radius (movement-internal; the world points
// view needs travel on events, so view never reads this).
constexpr aether::F32 kPlayerRadius = 16.0f;

// A downward-ground query result: distance to the nearest solid below + its
// surface id (for the wetness lookup).
struct GroundHit {
  aether::F32 distance = 0.0f;
  aether::U64 surface = 0;
};

// The collision world the system resolves against (shared component data; the
// game backs it with scene::Scene, tests with a fake floor).
struct WorldQuery {
  virtual ~WorldQuery() = default;
  [[nodiscard]] virtual aether::Vec2 ResolveCircle(
      aether::Vec2 center, aether::F32 radius) const = 0;
  [[nodiscard]] virtual std::optional<GroundHit> RaycastDown(
      aether::Vec2 origin) const = 0;
};

// The weather the system reads (shared component data written by the weather
// system): a lateral wind shove + per-surface wetness (which cuts traction).
struct WeatherQuery {
  virtual ~WeatherQuery() = default;
  [[nodiscard]] virtual aether::Vec3 WindAt(aether::Vec2 world) const = 0;
  [[nodiscard]] virtual aether::F32 WetnessOf(aether::U64 surface) const = 0;
};

// One fixed step of player intent (from the latched input).
struct PlayerInput {
  aether::F32 move = 0.0f;    // -1..1 horizontal steer
  bool jump_pressed = false;  // rising edge, latched this step
  bool jump_held = false;     // level, for variable jump height
};

// What one kinematic step produced: `position` is applied to the world,
// `landed`/`wall_hit` become events, `blocked` drives the camera nudge + turn.
struct PlayerStep {
  aether::Vec2 position{0.0f, 0.0f};
  aether::Vec2 blocked{0.0f, 0.0f};
  bool landed = false;
  bool wall_hit = false;
  aether::Vec2 wall_point{0.0f, 0.0f};
};

}  // namespace game
