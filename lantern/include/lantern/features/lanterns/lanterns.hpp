// The lanterns: which are lit, which one you last lit, and what that costs the
// renderer.
//
// A SIM feature — it decides STATE, never touches a light. Turning a lantern
// into an actual shadow-casting spot is `view`'s job, reading this state, which
// is what keeps the fixed step headless.
#pragma once

#include <vector>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace lantern::lanterns {

using aether::F32;
using aether::Usize;
using aether::Vec3;

struct Lantern {
  Vec3 position{0.0f, 0.0f, 0.0f};
  bool lit = false;
};

class Lanterns {
 public:
  void Add(Vec3 position) { items_.push_back(Lantern{.position = position}); }
  void Clear() { items_.clear(); }

  [[nodiscard]] const std::vector<Lantern>& Items() const { return items_; }
  [[nodiscard]] Usize Count() const { return items_.size(); }
  [[nodiscard]] Usize LitCount() const;

  // The nearest UNLIT lantern you are standing UNDER, or npos.
  //
  // `radius` is HORIZONTAL and `reach` is how far below the lantern still
  // counts — a column, not a sphere. Nearest by horizontal distance, so a
  // player between two lanterns lights the one they walked to.
  [[nodiscard]] Usize NearestUnlit(Vec3 from, F32 radius, F32 reach) const;

  // Light it, and remember it as the checkpoint. Returns false if it was
  // already lit, so the caller can tell a new light from a repeated one
  // without comparing counts.
  bool Light(Usize index);

  // Where a fall returns you. The last lantern LIT, not the nearest one — a
  // player who climbs past a lantern without lighting it has chosen the risk,
  // and quietly rescuing them to it would erase the choice.
  [[nodiscard]] bool HasCheckpoint() const {
    return checkpoint_ < items_.size();
  }
  [[nodiscard]] Vec3 Checkpoint() const;

 private:
  std::vector<Lantern> items_;
  Usize checkpoint_ = static_cast<Usize>(-1);
};

}  // namespace lantern::lanterns
