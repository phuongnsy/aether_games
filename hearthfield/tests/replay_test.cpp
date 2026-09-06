// Replay: the regression oracle every later milestone leans on.
//
// The load-bearing case is the one with an absence in it. `offline_ticks` is
// recorded like any other latched value, so a replay reproduces overnight
// growth to the tick WITHOUT ever reading a clock — which is the whole reason
// the wall clock was kept outside the step (spec §4.2).
#include "hf/runtime/replay.hpp"

#include <doctest/doctest.h>

#include <vector>

#include "hf/content/crops.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/runtime/game_world.hpp"

using namespace aether;
using hearthfield::plots::PlotsSystem;
using hearthfield::runtime::GameWorld;
using hearthfield::runtime::LatchedInput;
using hearthfield::runtime::ReplayRecorder;
using hearthfield::runtime::Tick;
namespace content = hearthfield::content;
namespace runtime = hearthfield::runtime;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;
constexpr U64 kSeed = 0xBEEF;

// A scripted session: sow two plots, go away long enough for both to ripen,
// harvest one, sow it again, go away again.
[[nodiscard]] std::vector<LatchedInput> Script() {
  const auto grow = static_cast<U32>(runtime::TicksFromSeconds(
      content::CropById(content::kWheat).grow_seconds));
  return {
      LatchedInput{.tap = true, .hovered = 0},
      LatchedInput{.tap = true, .hovered = 1},
      LatchedInput{.offline_ticks = grow + 120},
      LatchedInput{},
      LatchedInput{.tap = true, .hovered = 0},  // harvest
      LatchedInput{.tap = true, .hovered = 0},  // sow again
      LatchedInput{.offline_ticks = grow * 3},
      LatchedInput{},
  };
}

[[nodiscard]] U64 Play(const std::vector<LatchedInput>& steps,
                       ReplayRecorder* recorder) {
  GameWorld world(kSeed, 2);
  PlotsSystem plots;
  world.AddSystem(plots);
  for (const LatchedInput& input : steps) {
    if (recorder != nullptr) {
      recorder->Record(input);
    }
    world.Step(input, kDt);
  }
  return world.World().Digest();
}

}  // namespace

TEST_CASE("A RECORDED RUN REPLAYS TO AN IDENTICAL WORLD") {
  ReplayRecorder recorder;
  const U64 live = Play(Script(), &recorder);

  REQUIRE(recorder.Steps().size() == Script().size());
  const U64 replayed = Play(recorder.Steps(), nullptr);
  CHECK(replayed == live);
}

TEST_CASE("THE ABSENCE IS PART OF THE RECORDING, NOT PART OF THE CLOCK") {
  // Drop offline_ticks from the stream and the replay must DIVERGE. Without
  // this the previous case would pass on a recording that captured nothing —
  // the two runs would agree because neither had gone anywhere.
  ReplayRecorder recorder;
  const U64 live = Play(Script(), &recorder);

  std::vector<LatchedInput> without_gaps = recorder.Steps();
  for (LatchedInput& input : without_gaps) {
    input.offline_ticks = 0;
  }
  CHECK(Play(without_gaps, nullptr) != live);
}

TEST_CASE("a replay is stable across repetitions, and a seed still matters") {
  const std::vector<LatchedInput> script = Script();
  const U64 first = Play(script, nullptr);
  CHECK(Play(script, nullptr) == first);
  CHECK(Play(script, nullptr) == first);

  // The RNG is untouched by H0's systems, so the seed cannot change the
  // outcome YET — but it IS in the digest, so a world seeded differently is a
  // different world the moment orders start drawing from it (spec §5.1).
  GameWorld other(kSeed + 1, 2);
  PlotsSystem plots;
  other.AddSystem(plots);
  for (const LatchedInput& input : script) {
    other.Step(input, kDt);
  }
  CHECK(other.World().Digest() != first);
}
