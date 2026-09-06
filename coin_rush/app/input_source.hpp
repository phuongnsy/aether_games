// Where a fixed step's LatchedInput comes from: the interchangeable sources
// determinism rests on (live keyboard / capture autopilot / replay file).
#pragma once

#include <string>
#include <vector>

#include "aether/app/app.hpp"
#include "aether/core/error.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "cr/runtime/latched_input.hpp"

namespace game {

using namespace aether;  // NOLINT(google-build-using-namespace)

// The narrow facts a live/autopilot source reads from the game each step.
class InputWorld {
 public:
  virtual ~InputWorld() = default;
  [[nodiscard]] virtual bool PlayerOnGround() const = 0;
  // The virtual-stick / WASD steer (drag + movement keys combined).
  [[nodiscard]] virtual Vec2 SteerFromDrag(const app::AppContext& ctx) = 0;
  // Jump as an ACTION rather than two named keys, so the on-screen button and
  // the keyboard reach it identically. The press is CONSUMED from the frame
  // latch (a fixed step can run 0x on the edge's frame); the hold is level.
  [[nodiscard]] virtual bool TakeJumpPress(const app::AppContext& ctx) = 0;
  [[nodiscard]] virtual bool JumpHeld(const app::AppContext& ctx) const = 0;
};

class InputSource {
 public:
  virtual ~InputSource() = default;
  // Produce this step's input (called at the top of the fixed step).
  [[nodiscard]] virtual LatchedInput Next(const app::AppContext& ctx,
                                          F32 dt) = 0;
  // React to the step's result. Only the autopilot uses it (turn at walls);
  // live/replay ignore it.
  virtual void Observe(Vec2 /*blocked*/, bool /*on_ground*/, F32 /*dt*/) {}
};

// Live play: keyboard + drag stick, with the jump edge from the frame latch.
class LiveInputSource final : public InputSource {
 public:
  explicit LiveInputSource(InputWorld& world) : world_(world) {}
  [[nodiscard]] LatchedInput Next(const app::AppContext& ctx, F32 dt) override;

 private:
  InputWorld& world_;
};

// The headless-capture autopilot as a self-contained input script: run one way,
// hop on a cadence (or to climb a wall), turn on a timer to sweep the level.
class AutopilotInputSource final : public InputSource {
 public:
  explicit AutopilotInputSource(InputWorld& world) : world_(world) {}
  [[nodiscard]] LatchedInput Next(const app::AppContext& ctx, F32 dt) override;
  void Observe(Vec2 blocked, bool on_ground, F32 dt) override;

 private:
  InputWorld& world_;
  F32 jump_t_ = 0.0f;     // hop timer
  F32 dir_ = 1.0f;        // run direction (starts right, toward the exit)
  bool blocked_ = false;  // hit a wall last step → jump to climb
  F32 turn_t_ = 0.0f;     // turn timer (paces the level)
};

// Deterministic playback of a recorded run. Exhausted → neutral input.
class ReplayInputSource final : public InputSource {
 public:
  explicit ReplayInputSource(std::vector<LatchedInput> steps)
      : steps_(std::move(steps)) {}
  [[nodiscard]] LatchedInput Next(const app::AppContext& ctx, F32 dt) override;
  [[nodiscard]] bool Exhausted() const { return pos_ >= steps_.size(); }

 private:
  std::vector<LatchedInput> steps_;
  Usize pos_ = 0;
};

// Text serialization of a recorded run (one `move jump_pressed jump_held` line
// per step) — the harness's record/replay files. App-level (file I/O).
[[nodiscard]] Result<void> SaveReplay(const std::string& path,
                                      const std::vector<LatchedInput>& steps);
[[nodiscard]] Result<std::vector<LatchedInput>> LoadReplay(
    const std::string& path);

}  // namespace game
