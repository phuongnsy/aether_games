// Records every fixed step's LatchedInput so a run replays exactly (the
// determinism oracle). Save/load + input sources live in app/input_source.
#pragma once

#include <vector>

#include "cr/runtime/latched_input.hpp"

namespace game {

class ReplayRecorder {
 public:
  void Record(const LatchedInput& in) { steps_.push_back(in); }
  void Clear() { steps_.clear(); }
  [[nodiscard]] const std::vector<LatchedInput>& Steps() const {
    return steps_;
  }

 private:
  std::vector<LatchedInput> steps_;
};

}  // namespace game
