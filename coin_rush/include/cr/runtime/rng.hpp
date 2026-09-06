// The game's single seeded PRNG (splitmix64) — the fixed step draws only from
// this, so a run is reproducible for replay. Deterministic, not cryptographic.
#pragma once

#include "aether/core/types.hpp"

namespace game {

class Rng {
 public:
  explicit Rng(aether::U64 seed = 0x9E3779B97F4A7C15ULL) : state_(seed) {}

  void Seed(aether::U64 s) { state_ = s; }

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

 private:
  aether::U64 state_;
};

}  // namespace game
