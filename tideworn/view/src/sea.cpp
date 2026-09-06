#include "tideworn/view/sea.hpp"

#include <algorithm>

#include "aether/render/renderer.hpp"
#include "aether/render/water.hpp"

namespace tideworn::view {

using namespace aether;

render::WaterWaves SeaWaves(const runtime::ViewSnapshot& snapshot) {
  render::WaterWaves waves;
  waves.strength = 1.0f;
  waves.wind_speed = snapshot.wind_speed;
  // The peak grows with the wind — a storm sea is LONGER, not just steeper.
  // A gentle stand-in for the PM peak until the schedule owns fidelity.
  waves.peak_wavelength = std::clamp(8.0f + snapshot.wind_speed, 8.0f, 60.0f);
  // The macro swell must sit BELOW the peak (longer wavelength) — the default
  // 25 m is SHORTER than a storm's peak, which inverts the hierarchy the band
  // exists for (the water review's finding).
  waves.macro_wavelength =
      std::clamp(2.5f * waves.peak_wavelength, 40.0f, 150.0f);
  waves.wind_direction = Vec2{0.86f, 0.51f};
  waves.contact_foam_width = 0.34f;
  // The SIM clock, so a replayed voyage renders the same sea (never dt).
  waves.time = snapshot.clock_s;
  // The storm quality ladder — the measured p90 lever (the plan's p0 numbers):
  // full spectrum grazed the budget at wind 24, and a storm's short tail is
  // exactly what rain and foam hide anyway.
  waves.components = snapshot.band == voyage::Band::kStorm ? 10 : 14;
  return waves;
}

void ApplySeaState(render::Renderer& renderer,
                   const runtime::ViewSnapshot& snapshot, F32 plane_y) {
  renderer.SetWaterFresnel(render::kWaterIor);
  renderer.SetWaterWaves(SeaWaves(snapshot), plane_y);
  // Clear coastal water — extinction DISTANCES in metres, per channel.
  //
  // RE-TUNED for the diving loop (2026-08-16). The old {0.70, 3.0, 8.0} was
  // authored when extinction attenuated the VIEW path only; submerged geometry
  // is now lit through the same water, so a given object pays the column TWICE
  // (light down, then view back) and the old values went black within a few
  // metres. Doubling the optical path is not a reason to halve the sea's
  // colour, so the distances lengthen instead.
  //
  // Chosen against a stated criterion rather than by eye, so it can be argued
  // with: at 10 m the light path must leave blue >= 0.5 and green >= 0.2 (the
  // dive stays legible) while red falls below 0.05 (red dying IS the depth cue,
  // and keeping it would trade the hue shift for brightness). {2.5, 8, 18}
  // gives 0.57 / 0.29 / 0.018 there, and still 0.21 blue at the swim camera's
  // 28 m floor — dim, not black. It also lands near real clear-coastal water,
  // which is a check on the criterion rather than its source.
  renderer.SetWaterAbsorption(Vec3{2.5f, 8.0f, 18.0f},
                              Vec3{0.050f, 0.230f, 0.235f});
  render::WaterSurface surface;
  surface.deep_color = Vec3{0.0150f, 0.0700f, 0.1250f};
  surface.mesh_extent = 2500.0f;
  // 144 m, MEASURED (2026-08-17#14) rather than guessed. The tile is
  // camera-centred, so what it must cover scales with the orbit distance:
  // submerged geometry spans 31.6 m at the default distance, 78.3 m at 40 m and
  // 114.3 m at the orbit's own max of 60 m. The old 36 m therefore lost the
  // tile's HEIGHT channel over most of the frame whenever the player zoomed
  // out, and depth reverted to the flat plane (2026-08-17#5's error, up to 2.45
  // m of false depth on a storm trough) with nothing said about it.
  //
  // EXACTLY 4x the old 36 m, and that is the point: the renderer now holds the
  // texel size and scales resolution, so 4x the extent is 4x the resolution and
  // the sampled grid is IDENTICAL — 0.28125 m texels either way. A non-power-of
  // -two widening would have moved every texel and drifted the three waterline
  // oracles, which is exactly what a flat raise to 120 m did.
  surface.caustic_extent = 144.0f;
  // CAUSTICS ARE OFF HERE, and the REASON was re-measured 2026-08-17 because
  // the original one had gone stale — exactly the trap this file should not
  // set for the next reader.
  //
  // The old reason ("the sun contributes almost nothing to submerged geometry
  // here") is now FALSE. It was true when extinction was {0.70, 3.0, 8.0} and
  // mesh_pbr had no depth term at all; with the re-tuned distances killing the
  // directional term for every submerged fragment now visibly darkens the
  // diver and the school. The sun reaches, and at strength 4 the caustic
  // banding is plainly visible on the diver's body.
  //
  // The reason it stays off is different and smaller: tideworn is DEEP OCEAN
  // with no bed, so the only receivers are a diver, some fish and a hull —
  // small, moving objects that a caustic network cannot read across. The tile
  // costs 0.35 ms at this quality tier, which is 4% of the 8.33 ms budget for
  // an effect with nothing large enough to land on. The trigger is CONTENT: a
  // reef, a wreck or a shallow bank gives it a surface, and it is one line here
  // when one exists.
  // Rain pits the surface: raise the sub-pixel roughness floor with the
  // band, so a rained-on sea loses its sharp reflections (the real effect on
  // the sun's glitter, not a stylistic dimmer).
  if (snapshot.band == voyage::Band::kStorm) {
    surface.reflection_roughness += 0.050f;
  } else if (snapshot.band == voyage::Band::kGale) {
    surface.reflection_roughness += 0.025f;
  }
  renderer.SetWaterSurface(surface);
}

}  // namespace tideworn::view
