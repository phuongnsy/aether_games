// Movement controller exercised in isolation — the payoff of the narrow-seam
// design: a fake floor + stub weather assert the feel, no GPU.
#include <doctest/doctest.h>

#include <cmath>
#include <optional>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "cr/features/movement/system.hpp"

using namespace aether;

namespace {

// A single horizontal floor whose top surface is at y = 0. A player circle
// rests when its centre is one radius above the floor.
struct FlatGround : game::WorldQuery {
  U64 surface_id = 42;

  [[nodiscard]] Vec2 ResolveCircle(Vec2 c, F32 r) const override {
    return Vec2{c.x,
                std::max(c.y, r)};  // never let the circle sink below floor
  }
  [[nodiscard]] std::optional<game::GroundHit> RaycastDown(
      Vec2 o) const override {
    if (o.y < 0.0f) {
      return std::nullopt;
    }
    return game::GroundHit{.distance = o.y, .surface = surface_id};
  }
};

// Empty space: no collision, no ground below (free fall forever).
struct Void : game::WorldQuery {
  [[nodiscard]] Vec2 ResolveCircle(Vec2 c, F32 /*r*/) const override {
    return c;
  }
  [[nodiscard]] std::optional<game::GroundHit> RaycastDown(
      Vec2 /*o*/) const override {
    return std::nullopt;
  }
};

// Calm + dry: no wind, no wetness.
struct Calm : game::WeatherQuery {
  [[nodiscard]] Vec3 WindAt(Vec2 /*p*/) const override {
    return Vec3{0.0f, 0.0f, 0.0f};
  }
  [[nodiscard]] F32 WetnessOf(U64 /*s*/) const override { return 0.0f; }
};

// A steady lateral wind, dry ground.
struct SteadyWind : game::WeatherQuery {
  explicit SteadyWind(F32 wx) : wind_x(wx) {}
  F32 wind_x = 0.0f;

  [[nodiscard]] Vec3 WindAt(Vec2 /*p*/) const override {
    return Vec3{wind_x, 0.0f, 0.0f};
  }
  [[nodiscard]] F32 WetnessOf(U64 /*s*/) const override { return 0.0f; }
};

// Calm wind, fully-wet ground (traction loss).
struct Soaked : game::WeatherQuery {
  [[nodiscard]] Vec3 WindAt(Vec2 /*p*/) const override {
    return Vec3{0.0f, 0.0f, 0.0f};
  }
  [[nodiscard]] F32 WetnessOf(U64 /*s*/) const override { return 1.0f; }
};

constexpr F32 kDt = 1.0f / 60.0f;
const game::PlayerInput kIdle{};  // no move, no jump

// Run until the player rests: grounded AND vertical motion settled (ground-snap
// reports "grounded" before the resolve floor, so wait for the fall to finish).
game::PlayerStep SettleOnGround(game::PlayerController& pc,
                                const game::WorldQuery& world,
                                const game::WeatherQuery& weather, Vec2 start) {
  game::PlayerStep step{.position = start};
  for (int i = 0; i < 240; ++i) {
    step = pc.Advance(world, step.position, weather, kDt, kIdle);
    if (pc.OnGround() && std::abs(pc.Velocity().y) < 1.0f) {
      break;
    }
  }
  return step;
}

}  // namespace

TEST_CASE("gravity accelerates a player downward in free fall") {
  game::PlayerController pc;
  const Void world;
  const Calm weather;
  game::PlayerStep step{.position = Vec2{0.0f, 500.0f}};
  for (int i = 0; i < 10; ++i) {
    step = pc.Advance(world, step.position, weather, kDt, kIdle);
  }
  CHECK(pc.Velocity().y < 0.0f);    // falling
  CHECK(step.position.y < 500.0f);  // moved down
  CHECK_FALSE(pc.OnGround());
}

TEST_CASE("a falling player lands and rests one radius above the floor") {
  game::PlayerController pc;
  const FlatGround world;
  const Calm weather;
  const game::PlayerStep landed =
      SettleOnGround(pc, world, weather, Vec2{0.0f, 300.0f});

  CHECK(pc.OnGround());
  CHECK(landed.position.y == doctest::Approx(game::kPlayerRadius));
  CHECK(pc.Velocity().y == doctest::Approx(0.0f));  // vertical motion killed
}

TEST_CASE("a grounded player launches on a buffered jump") {
  game::PlayerController pc;
  const FlatGround world;
  const Calm weather;
  game::PlayerStep step =
      SettleOnGround(pc, world, weather, Vec2{0.0f, 100.0f});
  REQUIRE(pc.OnGround());

  step = pc.Advance(
      world, step.position, weather, kDt,
      game::PlayerInput{.move = 0.0f, .jump_pressed = true, .jump_held = true});

  CHECK(pc.Velocity().y > 300.0f);  // launched up (kJumpVel minus one step g)
  CHECK_FALSE(pc.OnGround());
}

TEST_CASE("no jump without ground contact (coyote expired)") {
  game::PlayerController pc;  // starts airborne
  const Void world;
  const Calm weather;
  const game::PlayerStep step = pc.Advance(
      world, Vec2{0.0f, 500.0f}, weather, kDt,
      game::PlayerInput{.move = 0.0f, .jump_pressed = true, .jump_held = true});
  CHECK(pc.Velocity().y < 0.0f);    // still just falling — the jump didn't fire
  CHECK(step.position.y < 500.0f);  // and it kept descending
}

TEST_CASE("wet ground cuts horizontal traction") {
  const FlatGround world;

  game::PlayerController dry_pc;
  const Calm dry;
  game::PlayerStep dry_step = SettleOnGround(dry_pc, world, dry, Vec2{0, 100});

  game::PlayerController wet_pc;
  const Soaked wet;
  game::PlayerStep wet_step = SettleOnGround(wet_pc, world, wet, Vec2{0, 100});

  const game::PlayerInput run{.move = 1.0f};
  for (int i = 0; i < 6; ++i) {  // accelerate right for a few steps
    dry_step = dry_pc.Advance(world, dry_step.position, dry, kDt, run);
    wet_step = wet_pc.Advance(world, wet_step.position, wet, kDt, run);
  }
  CHECK(wet_pc.Velocity().x > 0.0f);
  CHECK(wet_pc.Velocity().x < dry_pc.Velocity().x);  // slower to speed up
}

TEST_CASE("a skid flips direction quickly (tight, not slippery)") {
  game::PlayerController pc;
  const FlatGround world;
  const Calm weather;
  game::PlayerStep step = SettleOnGround(pc, world, weather, Vec2{0, 100});

  // Build up rightward speed.
  const game::PlayerInput right{.move = 1.0f};
  for (int i = 0; i < 30; ++i) {
    step = pc.Advance(world, step.position, weather, kDt, right);
  }
  REQUIRE(pc.Velocity().x > 150.0f);

  // Steer hard left: the skid (turn accel) should cross zero within a few
  // frames — a crisp reversal, not a long icy slide.
  const game::PlayerInput left{.move = -1.0f};
  int frames = 0;
  while (pc.Velocity().x > 0.0f && frames < 30) {
    step = pc.Advance(world, step.position, weather, kDt, left);
    ++frames;
  }
  CHECK(pc.Velocity().x <= 0.0f);
  CHECK(frames < 8);
}

TEST_CASE("wind shoves a standing player sideways") {
  const FlatGround world;

  game::PlayerController calm_pc;
  const Calm calm;
  game::PlayerStep calm_step =
      SettleOnGround(calm_pc, world, calm, Vec2{0, 100});

  game::PlayerController wind_pc;
  const SteadyWind wind{200.0f};
  game::PlayerStep wind_step =
      SettleOnGround(wind_pc, world, wind, Vec2{0, 100});

  for (int i = 0; i < 20; ++i) {  // no input — only wind moves us
    calm_step = calm_pc.Advance(world, calm_step.position, calm, kDt, kIdle);
    wind_step = wind_pc.Advance(world, wind_step.position, wind, kDt, kIdle);
  }
  CHECK(wind_pc.Velocity().x > 0.0f);  // pushed downwind (+x)
  CHECK(wind_step.position.x > calm_step.position.x);
}
