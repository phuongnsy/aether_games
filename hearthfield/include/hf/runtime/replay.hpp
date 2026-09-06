// Records every fixed step's LatchedInput so a run replays exactly — the
// determinism oracle. Writing the stream to a file is H2's; this is the
// in-memory half every test uses.
//
// It records `offline_ticks` like any other latched value, which is the point
// of the whole tick model: a replayed farm grows overnight exactly as the live
// one did, without the replay ever reading a clock.
#pragma once

#include <vector>

#include "hf/runtime/latched_input.hpp"

namespace hearthfield::runtime {

class ReplayRecorder {
 public:
  void Record(const LatchedInput& input) { steps_.push_back(input); }
  void Clear() { steps_.clear(); }
  [[nodiscard]] const std::vector<LatchedInput>& Steps() const {
    return steps_;
  }

 private:
  std::vector<LatchedInput> steps_;
};

}  // namespace hearthfield::runtime
