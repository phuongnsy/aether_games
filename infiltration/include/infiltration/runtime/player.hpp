// What the player IS, before anything moves it: the tuning §17.2.1 calls
// motion simulation, and the capsule whose `max_climb` decides which of the
// level's steps is a kerb and which is a wall.
//
// A SIM layer — no scene, no render, no device. Cut (a) of i2: data and one
// factory, so a digest change here would mean the extraction method is wrong
// rather than the code.
#pragma once

#include "aether/core/types.hpp"
#include <span>

#include "aether/anim/locomotion.hpp"
#include "aether/core/geometry_query.hpp"
#include "aether/nav/character_obstacles.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/nav/character.hpp"

namespace infiltration::runtime {

using namespace aether;  // NOLINT(google-build-using-namespace)


// §17.2.1's "motion simulation, collision detection", at the smallest honest
// scope. Named constants at the top in `platformer.cpp`'s shape, because that
// example is this engine's own precedent for a controller and §17.2.1's advice
// is to study one genre rather than generalise.
struct PlayerTuning {
  F32 walk_speed = 2.2f;  // m/s, the default gait
  F32 run_speed = 4.6f;   // m/s with the run modifier held
  F32 accel = 18.0f;      // m/s^2 toward the intent
  F32 brake = 22.0f;      // m/s^2 toward a stop — higher, so a stop is crisp
};

// The capsule and its limits. `max_climb` is what makes `kKerbTop` a kerb and
// `kBlockTop` a wall, so the level's constants and this one are one decision.
[[nodiscard]] inline nav::CharacterBody PlayerBody() {
  nav::CharacterBody body;
  body.radius = 0.35f;
  body.height = 1.8f;
  body.max_climb = 0.4f;
  body.max_slope_degrees = 45.0f;
  return body;
}


// Where a run begins, which the level's geometry decides — the open half,
// clear of the wall's end.
constexpr Vec3 kStart{-7.0f, 0.0f, -8.0f};

// The player's whole state in one place: these were eight separate members of
// a game class carrying a hundred. Cut (b) of i2 moves them and changes
// NOTHING else — same initialisers, same step, still driven from the app.
struct PlayerState {
  nav::CharacterBody body = PlayerBody();
  nav::CharacterMotion motion{.position = kStart, .grounded = true};
  Vec3 desired{};  // the smoothed intent, before collision
  PlayerTuning tune;
  Vec3 position{-7.0f, 0.0f, -8.0f};
  Vec3 velocity{};
  anim::LocomotionState loco{};
  F32 phase = 0.0f;
};
// One fixed step of the player: intent to acceleration, acceleration to a
// swept move, the move resolved against other characters, and the achieved
// velocity into the gait solver. The ORDER is the behaviour — it is the same
// order this ran in as a member of the game class (i2 cut c).
//
// Everything it used to reach for is a parameter now, which is what makes it
// a sim function: no scene, no device, nothing that draws.
void StepPlayer(PlayerState& player, Vec3 intent, const GeometryQuery3& walls,
                std::span<const nav::CharacterCapsule> capsules, U64 self_id,
                F32 half_extent, const anim::LocomotionConfig& loco, F32 dt);

}  // namespace infiltration::runtime
