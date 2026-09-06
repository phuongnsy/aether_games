#include "screens.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include "aether/app/app_events.hpp"
#include "aether/audio/audio_system.hpp"
#include "aether/core/math/ease.hpp"  // ease::CubicOut
#include "aether/input/edge_latch.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/platform/key.hpp"
#include "aether/ui/layout.hpp"
#include "cr/content/levels.hpp"  // LevelCount / LevelName (menu buttons)
#include "cr/view/hud.hpp"        // DrawPlayHud
#include "cr/view/theme.hpp"

namespace game {

using namespace aether;

namespace {
// Ease a value toward a target by a fixed step (used for the screen fades).
F32 Approach(F32 value, F32 target, F32 step) {
  if (value < target) {
    return std::min(value + step, target);
  }
  return std::max(value - step, target);
}
}  // namespace

// Re-frame the wide establishing shot so the menu backdrop shows the whole map
// again after a round (the play scene left the camera zoomed in on the player).
void MenuScreen::OnEnter(const app::AppContext& /*ctx*/) {
  api_.FrameEstablishing();
}

void MenuScreen::Animate(const app::AppContext& /*ctx*/, F32 dt) {
  t_ = Approach(t_, 1.0f, dt * 5.0f);
}

void MenuScreen::BuildUi(const app::AppContext& ctx, ui::Context& ui) {
  const F32 e = ease::CubicOut(t_);
  const Vec2 canvas = ui.Canvas();  // responsive design-space size
  const F32 w = canvas.x;
  const F32 h = canvas.y;
  ui.SetOpacity(e);
  ui.Panel(ui::Rect{.x = 0, .y = 0, .w = w, .h = h}, kUiScrim);

  const resources::Font* title = api_.TitleFont();
  constexpr std::string_view kSub = "collect every coin before time!";
  const F32 pad = 36.0f;
  const F32 bw = 240.0f;
  const F32 bh = 42.0f;
  // Auto-fit the panel to the widest content line so text never overflows.
  const F32 mw = std::max({ui.MeasureText("COIN RUSH", title).x,
                           ui.MeasureText(kSub).x, bw}) +
                 2.0f * pad;
  const F32 mh = 340.0f;
  const F32 mx = 0.5f * (w - mw);
  const F32 my = 0.5f * (h - mh) - (1.0f - e) * 30.0f;
  const F32 cx = mx + mw * 0.5f;
  api_.DrawPanel(ui, ui::Rect{.x = mx, .y = my, .w = mw, .h = mh});
  ui.LabelCentered(cx, my + 24, "COIN RUSH", kUiAccent, title);
  ui.LabelCentered(cx, my + 74, kSub, kUiTextDim);
  const F32 bx = cx - bw * 0.5f;
  // ONE COLUMN, AND THAT FIXES A LATENT OVERLAP. Quit used to be placed at
  // `my + 112 + 3.0f * 46.0f + 10.0f` — a literal 3 beside a loop bounded by
  // LevelCount(), which returns kLevels.size(). They agree only by coincidence:
  // a fourth level put Quit underneath the fourth button. Taking its row after
  // the loop means it cannot be told a stale count.
  constexpr F32 kRowGap = 4.0f;      // rows advanced by 46 = bh + 4
  constexpr F32 kQuitBreath = 6.0f;  // Quit sits one notch lower than a level
  ui::Stack column{
      ui::Rect{.x = bx, .y = my + 112.0f, .w = bw, .h = mh - 112.0f},
      ui::Axis::kVertical, kRowGap};
  // One button per level (a simple level-select): pick a level → play it.
  for (int i = 0; i < LevelCount(); ++i) {
    const std::string label = std::format("{}  {}", i + 1, LevelName(i));
    if (ui.Button(column.Take(bh), label)) {
      api_.StartLevel(i);
    }
  }
  column.Take(kQuitBreath);  // a spacer row, so Quit reads as separate
  if (ui.Button(column.Take(bh), "Quit")) {
    ctx.events.Publish(app::QuitRequested{});
  }
  ui.SetOpacity(1.0f);
}

void PlayScreen::OnEnter(const app::AppContext& ctx) {
  ctx.sim_input.Clear();  // drop any press made in the menu (see EdgeLatch)
  // Same reason, one level up: a suspend that happened on the MENU is not this
  // round's business, and leaving it latched opened every new level already
  // paused (observed on device). Nothing else consumes it there — the menu has
  // no pause to show — so the flag has to be dropped when play begins.
  (void)api_.TakeSuspendPause();
  ctx.audio.SetPaused(false);  // a round always begins audible
  api_.StartRound();
}

// Leaving play for the menu or the result screen: the world stops mattering, so
// neither should the ambience it was scoring.
void PlayScreen::OnExit(const app::AppContext& ctx) {
  ctx.audio.SetPaused(false);
}

void PlayScreen::OnObscure(const app::AppContext& ctx) {
  ctx.audio.SetPaused(true);
}

void PlayScreen::OnReveal(const app::AppContext& ctx) {
  ctx.audio.SetPaused(false);
}

void PlayScreen::HandleInput(const app::AppContext& ctx) {
  // The jump edge is latched by the engine into ctx.sim_input and consumed in
  // the fixed step (see input_source.cpp) — no per-frame latch needed here.
  //
  // Coming back from a suspend pauses, and it is checked FIRST so a resume can
  // never be mistaken for gameplay. `Take` consumes, so it fires once.
  if (api_.TakeSuspendPause() || ctx.input.JustPressed(platform::Key::kP)) {
    Stack().Push(std::make_unique<PauseScreen>(api_));
  }
}

void PlayScreen::Update(const app::AppContext& ctx, F32 dt) {
  api_.StepWorld(ctx, dt);
}

void PlayScreen::BuildUi(const app::AppContext& /*ctx*/, ui::Context& ui) {
  api_.DrawJoystick(ui);
  api_.DrawJumpButton(ui);
  DrawPlayHud(ui, api_.HudState());
  // A finger has no `P` key, so touch devices get a real button — and only they
  // do, so a desktop HUD is unchanged (and no capture digest moves). A `ui`
  // widget rather than art + an action-map region like jump: pausing is a UI
  // action, so it wants the press visuals and the WantsPointer claim that stops
  // the same tap also grabbing the steer stick.
  if (api_.TouchControlsVisible()) {
    // Below the clock, which owns the corner itself (see DrawPlayHud).
    const ui::Rect rect =
        ui.Anchored(ui::Anchor::kTopRight, 96.0f, 44.0f, Vec2{-18.0f, 52.0f});
    if (ui.Button(rect, "II")) {
      Stack().Push(std::make_unique<PauseScreen>(api_));
    }
  }
}

void PauseScreen::HandleInput(const app::AppContext& ctx) {
  // Consumed and dropped: a suspend WHILE paused is already showing the pause
  // menu, and leaving it latched would re-pause the moment play resumes.
  (void)api_.TakeSuspendPause();
  if (ctx.input.JustPressed(platform::Key::kP)) {
    Stack().Pop();
  }
}

void PauseScreen::Animate(const app::AppContext& /*ctx*/, F32 dt) {
  const F32 target = IsDismissed() ? 0.0f : 1.0f;  // eases out once popped
  t_ = Approach(t_, target, dt * 8.0f);
}

void PauseScreen::BuildUi(const app::AppContext& /*ctx*/, ui::Context& ui) {
  const F32 e = ease::CubicOut(t_);
  const Vec2 canvas = ui.Canvas();  // responsive design-space size
  const F32 w = canvas.x;
  const F32 h = canvas.y;
  ui.SetOpacity(e);
  ui.Panel(ui::Rect{.x = 0, .y = 0, .w = w, .h = h}, kUiScrim);
  const resources::Font* title = api_.TitleFont();
  const F32 bw = 200.0f;
  const F32 bh = 46.0f;
  const F32 mw = std::max(ui.MeasureText("Paused", title).x, bw) + 72.0f;
  const F32 mh = 200.0f;
  const F32 mx = 0.5f * (w - mw);
  const F32 my = 0.5f * (h - mh) + (1.0f - e) * 40.0f;
  const F32 cx = mx + mw * 0.5f;
  api_.DrawPanel(ui, ui::Rect{.x = mx, .y = my, .w = mw, .h = mh});
  ui.LabelCentered(cx, my + 22, "Paused", kUiText, title);
  const F32 bx = cx - bw * 0.5f;
  const bool live = !IsDismissed();
  ui::Stack column{ui::Rect{.x = bx, .y = my + 84.0f, .w = bw, .h = mh - 84.0f},
                   ui::Axis::kVertical, 8.0f};
  if (ui.Button(column.Take(bh), "Resume") && live) {
    Stack().Pop();
  }
  if (ui.Button(column.Take(bh), "Menu") && live) {
    api_.ToMenu();
  }
  ui.SetOpacity(1.0f);
}

void ResultScreen::Animate(const app::AppContext& /*ctx*/, F32 dt) {
  t_ = Approach(t_, 1.0f, dt * 5.0f);
}

void ResultScreen::BuildUi(const app::AppContext& /*ctx*/, ui::Context& ui) {
  const F32 e = ease::CubicOut(t_);
  const Vec2 canvas = ui.Canvas();  // responsive design-space size
  const F32 w = canvas.x;
  const F32 h = canvas.y;
  ui.SetOpacity(e);
  ui.Panel(ui::Rect{.x = 0, .y = 0, .w = w, .h = h}, kUiScrim);
  const resources::Font* title = api_.TitleFont();
  const std::string_view heading = won_ ? "YOU WIN!" : "TIME UP";
  const RoundResult r = api_.RoundSummary();
  const std::string score =
      std::format("coins {}/{}   stars {}/3", r.collected, r.total, r.stars);
  const F32 bw = 240.0f;
  const F32 bh = 48.0f;
  const F32 mw = std::max({ui.MeasureText(heading, title).x,
                           ui.MeasureText(score).x, bw}) +
                 72.0f;
  const F32 mh = 240.0f;
  const F32 mx = 0.5f * (w - mw);
  const F32 my = 0.5f * (h - mh) - (1.0f - e) * 24.0f;
  const F32 cx = mx + mw * 0.5f;
  api_.DrawPanel(ui, ui::Rect{.x = mx, .y = my, .w = mw, .h = mh});
  ui.LabelCentered(cx, my + 28, heading, won_ ? kUiGood : kUiBad, title);
  ui.LabelCentered(cx, my + 84, score, kUiText);
  const F32 bx = cx - bw * 0.5f;
  // Progression: advance to the next level if there is one, else replay.
  const bool has_next = won_ && api_.HasNextLevel();
  const std::string primary = has_next ? "Next Level" : "Play Again";
  ui::Stack column{
      ui::Rect{.x = bx, .y = my + 122.0f, .w = bw, .h = mh - 122.0f},
      ui::Axis::kVertical, 8.0f};
  if (ui.Button(column.Take(bh), primary)) {
    api_.NextLevel();  // advances if there is a next level, else replays
  }
  if (ui.Button(column.Take(bh), "Menu")) {
    api_.ToMenu();
  }
  ui.SetOpacity(1.0f);
}

}  // namespace game
