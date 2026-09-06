// The farm clock: one integer the fixed step owns, and the ONE place the step
// rate appears.
//
// Nothing below app/ may read a wall clock (games/CLAUDE.md, spec §4). A crop
// stores an absolute `ready_tick` on this counter and the step compares two
// integers it owns; elapsed real time reaches the sim only as
// LatchedInput::offline_ticks.
#pragma once

#include "aether/core/types.hpp"

namespace hearthfield::runtime {

// U64, not U32, and that is not caution. Every other game's counter dies with
// the process; this one ACCUMULATES ABSENCES, so its ceiling is calendar time
// rather than play time — a U32 at 60 Hz wraps after 2.27 years of wall clock,
// which a farm can reach and which would corrupt every ready_tick at once.
using Tick = aether::U64;

// Must equal 1/fixed_dt for the app that runs this game. The game pins
// `fixed_dt` in its config.json when app/ arrives at H1; until then nothing
// can disagree with it, because nothing else reads the rate.
inline constexpr aether::U32 kTicksPerSecond = 60;

// Rounds to nearest so a duration never silently loses its last fraction of a
// tick, and floors at 1: a crop that is ready the tick it was sown is an
// authoring slip, and content's static_assert already refuses one.
[[nodiscard]] constexpr Tick TicksFromSeconds(aether::F64 seconds) {
  if (seconds <= 0.0) {
    return Tick{1};
  }
  const auto ticks = static_cast<Tick>(
      seconds * static_cast<aether::F64>(kTicksPerSecond) + 0.5);
  return ticks == 0 ? Tick{1} : ticks;
}

[[nodiscard]] constexpr aether::F64 SecondsFromTicks(Tick ticks) {
  return static_cast<aether::F64>(ticks) /
         static_cast<aether::F64>(kTicksPerSecond);
}

}  // namespace hearthfield::runtime
