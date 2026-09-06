// Headless-sim smoke test: the whole fixed-step pipeline (movement→coins→
// progression) runs in GameWorld with NO device — proving the sim is headless.
#include <doctest/doctest.h>

#include "aether/scene_core/collider_component.hpp"
#include "aether/scene_core/scene.hpp"
#include "cr/features/coins/system.hpp"
#include "cr/features/movement/system.hpp"
#include "cr/features/progression/system.hpp"
#include "cr/runtime/game_world.hpp"

using namespace aether;

namespace {
constexpr U32 kWall = 0x1;
constexpr U32 kCoin = 0x2;
}  // namespace

TEST_CASE("headless sim: movement→coins→progression tallies a pickup") {
  scene::Scene scene;
  const scene::NodeId root = scene.Root();

  // A wide solid floor just below the player.
  const scene::NodeId floor = scene.CreateNode(root);
  scene.SetLocalTransform(floor, Transform{.position = Vec3{0.0f, 0.0f, 0.0f}});
  auto* fc = scene.AddComponent<scene::ColliderComponent>(floor);
  fc->kind = scene::ShapeKind::kBox;
  fc->half_extents = Vec2{400.0f, 8.0f};
  fc->layer = kWall;

  // The player, resting one radius above the floor top (y = 8).
  const scene::NodeId player = scene.CreateNode(root);
  scene.SetLocalTransform(
      player,
      Transform{.position = Vec3{0.0f, 8.0f + game::kPlayerRadius, 0.0f}});

  // A coin trigger right on the player.
  const scene::NodeId coin = scene.CreateNode(root);
  scene.SetLocalTransform(
      coin,
      Transform{.position = Vec3{0.0f, 8.0f + game::kPlayerRadius, 0.0f}});
  auto* cc = scene.AddComponent<scene::ColliderComponent>(coin);
  cc->kind = scene::ShapeKind::kCircle;
  cc->radius = 8.0f;
  cc->layer = kCoin;
  cc->trigger = true;

  // Stand up the world + the three sim systems, in order.
  game::GameWorld world;
  game::MovementSystem movement;
  game::CoinsSystem coins;
  game::ProgressSystem progression;
  world.AddSystem(movement);
  world.AddSystem(coins);
  world.AddSystem(progression);

  world.World().Bind(&scene, kWall, kCoin);
  world.World().SetPlayer(player);
  world.World().Env().SetWind(
      world.World().WindField());  // as SetupWeather does
  game::RoundProgress& pr = world.World().Progress();
  pr.total = 1;
  pr.time_left = 30.0f;
  pr.win_radius = 30.0f;
  pr.exit_pos = Vec2{10000.0f, 10000.0f};  // far away — no win this test

  // One fixed step: coins detects the overlap, progression tallies it.
  const game::EventList& events =
      world.Step(game::LatchedInput{}, 1.0f / 60.0f);

  bool saw_coin = false;
  for (const game::GameEvent& e : events) {
    if (std::holds_alternative<game::CoinCollected>(e)) {
      saw_coin = true;
    }
  }
  CHECK(saw_coin);                    // coins system emitted the event
  CHECK(pr.collected == 1);           // progression system tallied it
  CHECK(pr.exit_unlocked);            // all (1) coins collected → exit unlocks
  CHECK_FALSE(pr.round_over);         // exit is far, so no win
  CHECK(scene.Get(coin) == nullptr);  // the coin was removed from the world
}
