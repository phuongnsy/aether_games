// The weather — a PURE FUNCTION OF THE CLOCK, and nothing else.
//
// No state, nothing saved, nothing in the digest, and not one draw from the
// world RNG. Three things follow, and the third is why it is written this way:
//
//   1. AN ABSENCE IS FREE. Ask whether it is raining at tick 30,000,000 and get
//      an answer in a few instructions. A weather machine holding state would
//      have to be ADVANCED across the gap, which is the per-tick work the whole
//      offline model exists to avoid (spec risk 5).
//   2. THE WORLD RNG IS UNTOUCHED. Rolling for rain would shift the order
//      board's numbers and break every digest comparison in the project — and
//      it would break them only when RENDERING, because a headless test never
//      draws a raindrop. That is the worst shape a bug can have here.
//   3. IT COULD LATER DECIDE SOMETHING. A wet plot ripening faster is a sim
//      rule over a function the sim can already evaluate at any tick, past or
//      future. Not done (H5 §3e is presentation only) — but the shape does not
//      close the door, which a stateful one would.
#pragma once

#include "aether/core/types.hpp"
#include "hf/runtime/tick.hpp"

namespace hearthfield::runtime {

// A spell is the unit weather is decided in: one roll covers this many seconds,
// wet or dry throughout. Twenty minutes is two wheat harvests, long enough that
// the sky is not flickering and short enough that a session sees both.
inline constexpr aether::F64 kSpellSeconds = 1200.0;
// Roughly one spell in three. A number, not a probability distribution: the
// hash below is uniform, so this is exactly the fraction of spells that rain.
inline constexpr aether::U32 kRainPercent = 34;

[[nodiscard]] constexpr bool RainingAt(Tick tick) {
  const auto spell =
      static_cast<aether::U64>(tick / TicksFromSeconds(kSpellSeconds));
  // SplitMix64's finalizer. Chosen because it is a well-mixed bijection on 64
  // bits — consecutive spells must not correlate, and `spell * k % 100` would
  // give a visible period. Not the world RNG, and deliberately not: this must
  // be answerable for a tick the sim has never stepped through.
  aether::U64 hash = spell + 0x9E3779B97F4A7C15ULL;
  hash = (hash ^ (hash >> 30)) * 0xBF58476D1CE4E5B9ULL;
  hash = (hash ^ (hash >> 27)) * 0x94D049BB133111EBULL;
  hash ^= hash >> 31;
  return (hash % 100) < kRainPercent;
}

}  // namespace hearthfield::runtime
