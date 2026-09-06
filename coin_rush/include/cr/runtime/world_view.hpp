// The shared, mutable sim state features read/write during the fixed step — the
// ONLY channel besides events. Borrows the scene, owns the weather fields.

// Links aether::scene_core, so this sim layer reaches no renderer and no bgfx —
// checked by game_sim_layers_link_scene_core, not just intended.
#pragma once

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/surface/wetness_field.hpp"
#include "aether/worldsim/environment.hpp"
#include "aether/worldsim/wind_field.hpp"
#include "cr/runtime/progress.hpp"

namespace game {

class WorldView {
 public:
  // Bind the render scene (still app-owned) + the game's collision layers.
  void Bind(aether::scene::Scene* scene, aether::U32 solid_mask,
            aether::U32 coin_mask) {
    scene_ = scene;
    solid_mask_ = solid_mask;
    coin_mask_ = coin_mask;
  }
  void SetPlayer(aether::scene::NodeId player) { player_ = player; }

  [[nodiscard]] aether::scene::Scene& Scene() { return *scene_; }
  [[nodiscard]] aether::scene::NodeId Player() const { return player_; }
  [[nodiscard]] aether::U32 SolidMask() const { return solid_mask_; }
  [[nodiscard]] aether::U32 CoinMask() const { return coin_mask_; }

  // Owned shared weather state — the weather step writes it, movement reads it.
  [[nodiscard]] aether::worldsim::Environment& Env() { return env_; }
  [[nodiscard]] aether::worldsim::NoiseWindField& WindField() {
    return wind_field_;
  }
  [[nodiscard]] aether::surface::WetnessField& Wetness() { return wetness_; }

  // Round progression (coins/timer/win) — the progression system writes it, the
  // HUD + result screen read it.
  [[nodiscard]] RoundProgress& Progress() { return progress_; }
  [[nodiscard]] const RoundProgress& Progress() const { return progress_; }

  [[nodiscard]] aether::Vec3 WindAt(aether::Vec2 p) const {
    return env_.Wind().Sample(aether::Vec3{p.x, p.y, 0.0f}, 0.0f);
  }
  [[nodiscard]] aether::F32 WetnessOf(aether::U64 surface) const {
    return wetness_.Wetness(surface);
  }

 private:
  aether::scene::Scene* scene_ = nullptr;  // render scene (app-owned for now)
  aether::scene::NodeId player_;
  aether::U32 solid_mask_ = 0;
  aether::U32 coin_mask_ = 0;
  aether::worldsim::Environment env_;  // wind facade (SetWind → WindField)
  aether::worldsim::NoiseWindField wind_field_;
  aether::surface::WetnessField wetness_;  // per-surface wetness (movement)
  RoundProgress progress_;                 // round rules (coins/timer/win)
};

}  // namespace game
