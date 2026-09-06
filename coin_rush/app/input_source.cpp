#include "input_source.hpp"

#include <algorithm>
#include <fstream>
#include <string>

#include "aether/input/edge_latch.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/platform/key.hpp"

namespace game {

namespace {
// Capture-autopilot pacing (was game-side; lives with the autopilot now).
constexpr F32 kAutoJumpInterval = 0.85f;  // seconds per hop
constexpr F32 kAutoTurnInterval = 5.0f;   // seconds per turn (sweeps the level)
}  // namespace

LatchedInput LiveInputSource::Next(const app::AppContext& ctx, F32 /*dt*/) {
  const Vec2 steer = world_.SteerFromDrag(ctx);
  const input::InputSnapshot& keys = ctx.input;
  LatchedInput in;
  in.move =
      std::clamp(steer.x + (keys.IsDown(platform::Key::kRight) ? 1.0f : 0.0f) -
                     (keys.IsDown(platform::Key::kLeft) ? 1.0f : 0.0f),
                 -1.0f, 1.0f);
  // Jump PRESS comes from the engine's per-frame edge latch (never JustPressed
  // here: a fixed step can run 0× on the edge's frame). It is an ACTION now,
  // so the keyboard and the on-screen touch button arrive by one path and the
  // action map consumes every bound source without short-circuiting.
  in.jump_pressed = world_.TakeJumpPress(ctx);
  in.jump_held = world_.JumpHeld(ctx);
  return in;
}

LatchedInput AutopilotInputSource::Next(const app::AppContext& /*ctx*/,
                                        F32 dt) {
  jump_t_ -= dt;
  LatchedInput in;
  in.move = dir_;       // run toward the exit
  in.jump_held = true;  // always float (variable-height); press gates the hop
  if (world_.PlayerOnGround() && (jump_t_ <= 0.0f || blocked_)) {
    in.jump_pressed = true;  // hop on a cadence, or to climb a blocking wall
    jump_t_ = kAutoJumpInterval;
  }
  return in;
}

void AutopilotInputSource::Observe(Vec2 blocked, bool on_ground, F32 dt) {
  blocked_ = std::abs(blocked.x) > 0.5f && on_ground;
  turn_t_ -= dt;
  if (turn_t_ <= 0.0f) {  // turn around so the run sweeps the whole level
    dir_ = -dir_;
    turn_t_ = kAutoTurnInterval;
  }
}

LatchedInput ReplayInputSource::Next(const app::AppContext& /*ctx*/,
                                     F32 /*dt*/) {
  return pos_ < steps_.size() ? steps_[pos_++] : LatchedInput{};
}

Result<void> SaveReplay(const std::string& path,
                        const std::vector<LatchedInput>& steps) {
  std::ofstream out(path);
  if (!out) {
    return Fail(Errc::kIoError, "cannot open replay file for writing");
  }
  for (const LatchedInput& s : steps) {
    out << s.move << ' ' << (s.jump_pressed ? 1 : 0) << ' '
        << (s.jump_held ? 1 : 0) << '\n';
  }
  if (!out) {
    return Fail(Errc::kIoError, "error writing replay file");
  }
  return Ok();
}

Result<std::vector<LatchedInput>> LoadReplay(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return Fail(Errc::kNotFound, "cannot open replay file for reading");
  }
  std::vector<LatchedInput> steps;
  F32 move = 0.0f;
  int jp = 0;
  int jh = 0;
  while (in >> move >> jp >> jh) {
    steps.push_back(LatchedInput{
        .move = move, .jump_pressed = jp != 0, .jump_held = jh != 0});
  }
  if (in.bad()) {
    return Fail(Errc::kParseError, "malformed replay file");
  }
  return steps;
}

}  // namespace game
