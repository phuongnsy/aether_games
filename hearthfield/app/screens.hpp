// The in-game GUI: the barn, the shop and the order board.
//
// GEA §1.6.8.4's third front-end category — "an in-game graphical user
// interface, allowing the player to manipulate his or her character's inventory
// … or perform other complex in-game tasks" — as distinct from the HUD, which
// is always on and lives in view/.
//
// In app/ because AGENTS.md puts screens there, and because they act
// through runtime::GameApi: EVERY button writes a latched field and nothing
// else. A screen that reached into the world would work perfectly and silently
// end the replay (H4 plan §3b).
#pragma once

#include "aether/resources/texture.hpp"
#include "aether/ui/screen.hpp"
#include "hf/runtime/game_api.hpp"
#include "hf/runtime/snapshot.hpp"

namespace hearthfield::app {

// What a screen is handed each frame: the verbs, and the picture. Rebuilt every
// frame by the game — `api` borrows the pending latch, which app/ clears after
// each fixed step.
struct ScreenCtx {
  runtime::GameApi api;
  const runtime::ViewSnapshot* snapshot = nullptr;
  bool close_requested = false;  // Esc, read by the top screen
  // The rounded frame every screen draws itself in. NULL is supported: the
  // panel falls back to a flat fill, which is what it was before s3 and what a
  // build with the texture missing still gets.
  const aether::resources::Texture* panel = nullptr;
};

using Screen = aether::ui::Screen<ScreenCtx>;
using ScreenStack = aether::ui::ScreenStack<ScreenCtx>;

// A scrolling LIST of what the farm owns.
class BarnScreen final : public Screen {
 public:
  void HandleInput(ScreenCtx& ctx) override;
  void BuildUi(ScreenCtx& ctx, aether::ui::Context& ui) override;
};

// A scrolling GRID of what coin buys: land, barn capacity, seed packets.
class ShopScreen final : public Screen {
 public:
  void HandleInput(ScreenCtx& ctx) override;
  void BuildUi(ScreenCtx& ctx, aether::ui::Context& ui) override;
};

// The board, one row per slot, with a Fill the barn can or cannot satisfy.
class OrdersScreen final : public Screen {
 public:
  void HandleInput(ScreenCtx& ctx) override;
  void BuildUi(ScreenCtx& ctx, aether::ui::Context& ui) override;
};

}  // namespace hearthfield::app
