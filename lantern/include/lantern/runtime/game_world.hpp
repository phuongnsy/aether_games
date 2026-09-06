// GameWorld — the deterministic fixed step.
//
// Takes the Scene by reference rather than owning it: instantiating a world
// needs `app::EngineSpawners` and a material factory, which live above this
// layer. Runtime links aether::scene for the collision model and NO render,
// which is what lets the step run headless (games/CLAUDE.md, dependency law 4).
#pragma once

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "lantern/features/climb/walker.hpp"
#include "lantern/features/lanterns/lanterns.hpp"
#include "lantern/features/weather/weather.hpp"
#include "lantern/runtime/events.hpp"
#include "lantern/runtime/snapshot.hpp"

namespace aether::scene {
class Scene;
}

namespace lantern::runtime {

struct Input {
  aether::Vec2 move{0.0f, 0.0f};
  bool jump = false;
  bool interact = false;
};

class GameWorld {
 public:
  void Reset(aether::Vec3 start);
  [[nodiscard]] lanterns::Lanterns& Lanterns() { return lanterns_; }
  [[nodiscard]] climb::Walker& Walker() { return walker_; }
  [[nodiscard]] weather::WeatherSim& Weather() { return weather_; }
  [[nodiscard]] const weather::WeatherSim& Weather() const { return weather_; }

  // One fixed step. Events are CLEARED and refilled, so a caller that forgets
  // to drain them gets this frame's, never a growing pile.
  void Step(aether::scene::Scene& scene, const Input& input, F32 dt);

  [[nodiscard]] const Events& Frame() const { return events_; }
  [[nodiscard]] ViewSnapshot Snapshot() const;

 private:
  climb::Walker walker_;
  lanterns::Lanterns lanterns_;
  weather::WeatherSim weather_;
  Events events_;
  aether::Vec3 start_{0.0f, 0.0f, 0.0f};
  bool was_grounded_ = false;
  F32 fall_speed_ = 0.0f;
};

}  // namespace lantern::runtime
