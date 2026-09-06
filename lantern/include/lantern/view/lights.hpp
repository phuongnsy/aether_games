// Lit state -> actual lights. PRESENTATION, deliberately: the sim decides which
// lanterns are lit and never touches a PunctualLightComponent, which is what
// keeps the fixed step free of the render layer.
//
// This is composition check 4 in one function — a lantern lit at RUNTIME starts
// casting, and the shadow atlas re-solves for it on the next frame.
#pragma once

#include <vector>

#include "aether/scene_core/scene.hpp"
#include "lantern/runtime/snapshot.hpp"

namespace lantern::view {

// Matches the spire's authored intensity — the world file ships lanterns at 0
// so an unlit spire is genuinely dark, and this is what lighting one restores.
inline constexpr aether::F32 kLitIntensity = 40.0f;

class Lights {
 public:
  // The scene nodes carrying each lantern's spot light, parallel to the
  // snapshot's lanterns.
  void Bind(std::vector<aether::scene::NodeId> nodes) {
    nodes_ = std::move(nodes);
  }

  void Sync(aether::scene::Scene& scene, const runtime::ViewSnapshot& snapshot);

 private:
  std::vector<aether::scene::NodeId> nodes_;
};

}  // namespace lantern::view
