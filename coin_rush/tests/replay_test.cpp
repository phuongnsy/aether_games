// Replay determinism: the fixed step is a pure function of (world, input, dt),
// with no RNG or wall clock. Runs a script twice, asserts identical outcomes.
#include <doctest/doctest.h>

#include <vector>

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
constexpr F32 kDt = 1.0f / 60.0f;

struct Outcome {
  F32 player_x = 0.0f;
  F32 player_y = 0.0f;
  int collected = 0;
  F32 time_left = 0.0f;
  bool round_over = false;
};

// Build a fresh little arena (floor + two coins on the run path + player), then
// drive it through `script`, returning the final observable state.
Outcome RunScript(const std::vector<game::LatchedInput>& script) {
  scene::Scene scene;
  const scene::NodeId root = scene.Root();

  const scene::NodeId floor = scene.CreateNode(root);
  scene.SetLocalTransform(floor, Transform{.position = Vec3{0.0f, 0.0f, 0.0f}});
  auto* fc = scene.AddComponent<scene::ColliderComponent>(floor);
  fc->kind = scene::ShapeKind::kBox;
  fc->half_extents = Vec2{1000.0f, 8.0f};
  fc->layer = kWall;

  const F32 feet = 8.0f + game::kPlayerRadius;
  for (const F32 x : {60.0f, 140.0f}) {  // coins along the rightward run
    const scene::NodeId c = scene.CreateNode(root);
    scene.SetLocalTransform(c, Transform{.position = Vec3{x, feet, 0.0f}});
    auto* cc = scene.AddComponent<scene::ColliderComponent>(c);
    cc->kind = scene::ShapeKind::kCircle;
    cc->radius = 8.0f;
    cc->layer = kCoin;
    cc->trigger = true;
  }

  const scene::NodeId player = scene.CreateNode(root);
  scene.SetLocalTransform(player,
                          Transform{.position = Vec3{0.0f, feet, 0.0f}});

  game::GameWorld world(/*seed=*/1234);
  game::MovementSystem movement;
  game::CoinsSystem coins;
  game::ProgressSystem progression;
  world.AddSystem(movement);
  world.AddSystem(coins);
  world.AddSystem(progression);
  world.World().Bind(&scene, kWall, kCoin);
  world.World().SetPlayer(player);
  world.World().Env().SetWind(world.World().WindField());
  game::RoundProgress& pr = world.World().Progress();
  pr.total = 2;
  pr.time_left = 30.0f;
  pr.win_radius = 30.0f;
  pr.exit_pos = Vec2{100000.0f, 0.0f};  // unreachable — isolate movement/coins

  for (const game::LatchedInput& in : script) {
    world.Step(in, kDt);
  }
  const scene::Node* p = scene.Get(player);
  return Outcome{.player_x = p->local.position.x,
                 .player_y = p->local.position.y,
                 .collected = pr.collected,
                 .time_left = pr.time_left,
                 .round_over = pr.round_over};
}

}  // namespace

TEST_CASE("replay: the same input script reproduces the run exactly") {
  // A deterministic script: run right, hop on a cadence (variable-height taps).
  std::vector<game::LatchedInput> script;
  script.reserve(200);
  for (int i = 0; i < 200; ++i) {
    script.push_back(game::LatchedInput{.move = 1.0f,
                                        .jump_pressed = (i % 45 == 0),
                                        .jump_held = (i % 45 < 8)});
  }

  const Outcome a = RunScript(script);
  const Outcome b = RunScript(script);  // replay the identical script

  CHECK(a.player_x == doctest::Approx(b.player_x));
  CHECK(a.player_y == doctest::Approx(b.player_y));
  CHECK(a.collected == b.collected);
  CHECK(a.time_left == doctest::Approx(b.time_left));
  CHECK(a.round_over == b.round_over);

  // Sanity: the script actually did something (moved + collected the coins).
  CHECK(a.player_x > 100.0f);
  CHECK(a.collected == 2);
}
