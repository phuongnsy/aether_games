#include "cr/view/effects.hpp"

namespace game {

using namespace aether;

effects::ParticleConfig ToParticleConfig(const resources::EmitterDef& e) {
  effects::ParticleConfig c;
  c.capacity = e.capacity;
  c.spawn_rate = e.spawn_rate;
  c.lifetime = e.lifetime;
  c.lifetime_variance = e.lifetime_variance;
  c.gravity = e.gravity;
  c.direction = e.direction;
  c.spread_radians = e.spread_radians;
  c.speed_min = e.speed_min;
  c.speed_max = e.speed_max;
  c.emit_jitter = e.emit_jitter;
  c.start_color = e.start_color;
  c.end_color = e.end_color;
  c.start_size = e.start_size;
  c.end_size = e.end_size;
  c.texture = e.texture ? e.texture->Handle() : TextureHandle{};
  c.layer = e.layer;
  c.blend = e.blend;
  c.seed = e.seed;
  return c;
}

void EffectRuntime::SpawnAt(Vec2 pos) {
  for (Usize i = 0; i < systems.size(); ++i) {
    systems[i]->System().SetEmitterPosition(pos);
    systems[i]->System().Burst(counts[i]);
  }
}

EffectRuntime BuildEffect(
    scene::Scene& scene,
    const resources::ResourceHandle<resources::EffectResource>& fx) {
  EffectRuntime rt;
  if (!fx) {
    return rt;
  }
  for (const resources::EmitterDef& e : fx->Emitters()) {
    const scene::NodeId n = scene.CreateNode(scene.Root());
    auto* pc =
        scene.AddComponent<scene::ParticleComponent>(n, ToParticleConfig(e));
    pc->System().SetEmitting(false);  // burst-only
    rt.systems.push_back(pc);
    rt.counts.push_back(e.burst_count);
  }
  return rt;
}

}  // namespace game
