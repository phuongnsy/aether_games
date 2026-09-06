#include "lantern/features/lanterns/lanterns.hpp"

#include <algorithm>

namespace lantern::lanterns {

Usize Lanterns::LitCount() const {
  return static_cast<Usize>(
      std::ranges::count_if(items_, [](const Lantern& l) { return l.lit; }));
}

Usize Lanterns::NearestUnlit(Vec3 from, F32 radius, F32 reach) const {
  Usize best = static_cast<Usize>(-1);
  F32 best_distance = radius * radius;
  for (Usize i = 0; i < items_.size(); ++i) {
    if (items_[i].lit) {
      continue;
    }
    const Vec3 delta = items_[i].position - from;
    // ABOVE you and within reach. The lower bound matters as much as the
    // upper: a lantern below you is one you have already climbed past, and
    // reaching down to it would undo the ascent it marks.
    if (delta.y < 0.0f || delta.y > reach) {
      continue;
    }
    const F32 distance = (delta.x * delta.x) + (delta.z * delta.z);
    if (distance <= best_distance) {
      best_distance = distance;
      best = i;
    }
  }
  return best;
}

bool Lanterns::Light(Usize index) {
  if (index >= items_.size() || items_[index].lit) {
    return false;
  }
  items_[index].lit = true;
  checkpoint_ = index;
  return true;
}

Vec3 Lanterns::Checkpoint() const {
  return HasCheckpoint() ? items_[checkpoint_].position : Vec3{};
}

}  // namespace lantern::lanterns
