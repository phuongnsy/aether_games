#include "lantern/view/lights.hpp"

#include "aether/scene_core/punctual_light_component.hpp"

namespace lantern::view {

void Lights::Sync(aether::scene::Scene& scene,
                  const runtime::ViewSnapshot& snapshot) {
  const aether::Usize count = std::min(nodes_.size(), snapshot.lanterns.size());
  for (aether::Usize i = 0; i < count; ++i) {
    auto* light =
        scene.GetComponent<aether::scene::PunctualLightComponent>(nodes_[i]);
    if (light == nullptr) {
      continue;
    }
    const bool lit = snapshot.lanterns[i].lit;
    // An unlit lantern is DARK, not merely shadowless: intensity 0 also drops
    // it from the shadow-view budget, so four unlit lanterns cost nothing.
    light->SetIntensity(lit ? kLitIntensity : 0.0f);
    light->SetCastsShadows(lit);
  }
}

}  // namespace lantern::view
