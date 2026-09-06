// The narrow contract the flow screens drive the game through — a handful of
// verbs, never CoinRushGame's internals (no friendship). CoinRushGame impls it.
#pragma once

#include "aether/app/app.hpp"
#include "aether/core/types.hpp"
#include "aether/resources/font.hpp"
#include "aether/ui/context.hpp"
#include "aether/ui/screen.hpp"
#include "cr/runtime/view_snapshot.hpp"  // PlayHud

namespace game {

using namespace aether;  // NOLINT(google-build-using-namespace)

using GameScreen = ui::Screen<const app::AppContext>;
using GameStack = ui::ScreenStack<const app::AppContext>;

// The end-of-round summary the result screen shows.
struct RoundResult {
  int collected = 0;
  int total = 0;
  int stars = 0;
};

class GameApi {
 public:
  virtual ~GameApi() = default;

  // --- flow ---
  virtual void StartLevel(int index) = 0;  // menu: pick a level → play
  virtual void StartRound() = 0;  // (re)build + begin the current level
  virtual void NextLevel() = 0;   // result: advance, or replay if last
  virtual void ToMenu() = 0;
  virtual void FrameEstablishing() = 0;  // wide menu-backdrop framing

  // --- live play ---
  virtual void StepWorld(const app::AppContext& ctx,
                         F32 dt) = 0;  // one fixed step

  // --- presentation the screens draw with ---
  [[nodiscard]] virtual const resources::Font* TitleFont() const = 0;
  virtual void DrawPanel(ui::Context& ui, const ui::Rect& rect) = 0;
  virtual void DrawJoystick(ui::Context& ui) = 0;
  // The touch jump button, drawn only while playing. Its art comes from the
  // same normalized rect the action map binds, so the picture and the hitbox
  // cannot drift apart (docs/plans/2026-08-22-android-touch-input.md k5).
  virtual void DrawJumpButton(ui::Context& ui) = 0;
  [[nodiscard]] virtual PlayHud HudState() const = 0;

  // Whether this device has a touch screen, so screens can show the controls
  // a finger needs and hide them where a keyboard is the real input. Answered
  // by observation (a contact has arrived), not by an #ifdef.
  [[nodiscard]] virtual bool TouchControlsVisible() const = 0;

  // Consume a pending "the app was suspended" pause, if one waits. The play
  // screen is what acts on it, because pausing is only meaningful there — a
  // suspend during the menu must not push a pause menu over it.
  [[nodiscard]] virtual bool TakeSuspendPause() = 0;

  // --- state the screens display ---
  [[nodiscard]] virtual bool HasNextLevel() const = 0;
  [[nodiscard]] virtual RoundResult RoundSummary() const = 0;
};

}  // namespace game
