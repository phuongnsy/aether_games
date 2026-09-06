// The Coin Rush game-flow screens (menu → play ↔ pause, win/lose): thin
// ui::Screen controllers driving the game through GameApi, not CoinRushGame.
#pragma once

#include "game_api.hpp"

namespace game {

class MenuScreen final : public GameScreen {
 public:
  explicit MenuScreen(GameApi& api) : api_(api) {}
  void OnEnter(const app::AppContext& ctx) override;
  void Animate(const app::AppContext& ctx, F32 dt) override;
  void BuildUi(const app::AppContext& ctx, ui::Context& ui) override;

 private:
  GameApi& api_;
  F32 t_ = 0.0f;
};

class PlayScreen final : public GameScreen {
 public:
  explicit PlayScreen(GameApi& api) : api_(api) {}
  void OnEnter(const app::AppContext& ctx) override;
  void OnExit(const app::AppContext& ctx) override;
  // The world freezes when this screen is covered (ScreenStack updates only the
  // top), so the AUDIO has to be told separately — it lives on another thread
  // and nothing about being obscured reaches it.
  void OnObscure(const app::AppContext& ctx) override;
  void OnReveal(const app::AppContext& ctx) override;
  void HandleInput(const app::AppContext& ctx) override;
  void Update(const app::AppContext& ctx, F32 dt) override;
  void BuildUi(const app::AppContext& ctx, ui::Context& ui) override;

 private:
  GameApi& api_;
};

class PauseScreen final : public GameScreen {
 public:
  explicit PauseScreen(GameApi& api) : api_(api) {}
  void HandleInput(const app::AppContext& ctx) override;
  void Animate(const app::AppContext& ctx, F32 dt) override;
  void BuildUi(const app::AppContext& ctx, ui::Context& ui) override;
  [[nodiscard]] bool StillClosing() const override { return t_ > 0.001f; }

 private:
  GameApi& api_;
  F32 t_ = 0.0f;
};

// One screen for both endings — the message + color differ.
class ResultScreen final : public GameScreen {
 public:
  ResultScreen(GameApi& api, bool won) : api_(api), won_(won) {}
  void Animate(const app::AppContext& ctx, F32 dt) override;
  void BuildUi(const app::AppContext& ctx, ui::Context& ui) override;

 private:
  GameApi& api_;
  bool won_;
  F32 t_ = 0.0f;
};

}  // namespace game
