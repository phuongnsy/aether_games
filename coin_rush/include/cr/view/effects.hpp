// Runtime particle-effect helpers: turn a loaded EffectResource (vfx_studio)
// into a burst-only instance fired at a world point (the EmitterDef map).
#pragma once

#include <vector>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/effects/particle_system.hpp"
#include "aether/resources/effect_resource.hpp"
#include "aether/resources/resource_handle.hpp"
#include "aether/scene_core/particle_component.hpp"
#include "aether/scene_core/scene.hpp"

namespace game {

// A vfx_studio emitter (neutral resources::EmitterDef) → the effects layer's
// ParticleConfig.
aether::effects::ParticleConfig ToParticleConfig(
    const aether::resources::EmitterDef& e);

// A runtime instance of an EffectResource: one burst-only ParticleComponent per
// emitter, fired together at a point (composed effects = several emitters).
struct EffectRuntime {
  std::vector<aether::scene::ParticleComponent*> systems;
  std::vector<aether::Usize> counts;

  void SpawnAt(aether::Vec2 pos);
};

// Turn a loaded EffectResource into a runtime instance, parented to the scene
// root so it emits in world space. Empty runtime if the resource is absent.
EffectRuntime BuildEffect(
    aether::scene::Scene& scene,
    const aether::resources::ResourceHandle<aether::resources::EffectResource>&
        fx);

}  // namespace game
