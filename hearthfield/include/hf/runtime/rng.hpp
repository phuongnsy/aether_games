// The world's single seeded PRNG (splitmix64) — the fixed step draws only from
// this, so a run replays exactly. Deterministic, not cryptographic.
//
// Chosen over std::mt19937 for ONE reason that is Hearthfield-specific: its
// whole state is a single U64, so a save can round-trip it (spec §4.4 — a farm
// reloaded with a fresh RNG is a different farm). Mersenne's 624 words would
// need a serializer of its own or an implementation-defined stream operator.
#pragma once

#include "aether/core/types.hpp"

namespace hearthfield::runtime {

class Rng {
 public:
  explicit Rng(aether::U64 seed = 0x9E3779B97F4A7C15ULL) : state_(seed) {}

  aether::U64 NextU64() {
    state_ += 0x9E3779B97F4A7C15ULL;
    aether::U64 z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  // Uniform in [0, 1).
  aether::F32 NextF32() {
    return static_cast<aether::F32>(NextU64() >> 40) * (1.0f / 16777216.0f);
  }

  // Uniform in [0, bound). Modulo-biased, which is fine for a board that asks
  // for three loaves and would not be for anything a player could exploit.
  aether::U32 NextBelow(aether::U32 bound) {
    return bound == 0 ? 0 : static_cast<aether::U32>(NextU64() % bound);
  }

  // The save seam: the whole generator IS this value.
  [[nodiscard]] aether::U64 State() const { return state_; }
  void SetState(aether::U64 state) { state_ = state; }

 private:
  aether::U64 state_;
};

}  // namespace hearthfield::runtime
