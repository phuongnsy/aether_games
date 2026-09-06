// Rain over the sky world, and the wet soil it leaves.
//
// TWO LAYERS, and the split is the 2026-07-19 rain-interaction plan's own
// "performance contract" — a few hundred drops that COLLIDE, backed by a free
// visual layer of thousands that do not. This game built the colliding half in
// H5 and never built the other, so rain was 420 drops in an 11 m box sized to
// the farm board while the camera zooms out to see 160 m of archipelago: a
// drizzle-patch in the middle of a world, which is what "only a small amount"
// means.
//
// - `rain_` is the GAMEPLAY layer. It sweeps drops against one ground collider
//   and its impacts are what wetness is made of, so it is not optional and
//   nothing here may replace it.
// - `sky_` is the BACKGROUND layer: a GPU emitter, no collision, no impacts, no
//   readback (ADR-0013 class 2). It is created best-effort and its absence
//   costs the appearance and nothing else — compute is what it needs and WebGL2
//   has none, so on web this game keeps the rain it already had.
//
// ALL OF IT IS PRESENTATION, and that is the load-bearing decision (H5 plan
// §3e). Drops, impacts, wetness and its decay are stepped with the RENDER dt
// and live nowhere in the world state, the save or the digest. Lantern puts its
// splashes in the sim so a replay splashes identically; lantern has no absence
// model. This game does, and a continuous float in the world would mean an
// eight-hour gap had to simulate weather — the one thing spec risk 5 forbids.
//
// WHETHER it rains is not decided here either: `runtime::RainingAt` is a pure
// function of the tick, so the sky survives a month away for free and never
// touches the world RNG whose state the save carries.
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "aether/core/field.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/core/types.hpp"
#include "aether/render/gpu_particles.hpp"
#include "aether/resources/texture.hpp"
#include "aether/rhi/device.hpp"
#include "aether/scene_core/geometry_query.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/surface/wetness_field.hpp"
#include "aether/weather/rain_field3.hpp"
#include "hf/runtime/grid.hpp"

namespace hearthfield::view {

class WeatherFx {
 public:
  // Builds the streak texture and the rain field, and adds the ONE ground
  // collider the drops are swept against. Call after the board exists.
  //
  // ONE SURFACE, NOT 225 (plan §3f). `WetnessField` is uniform per surface by
  // design — its own header says wetness darkens the WHOLE surface — so a
  // collider per plot would cost 225 box sweeps per drop per frame to buy a
  // mottled board nobody asked for.
  [[nodiscard]] aether::Result<void> Create(aether::rhi::Device& device,
                                            aether::scene::Scene& scene,
                                            const runtime::Grid& grid);

  // One rendered frame: emit or hold the drops, resolve impacts into wetness,
  // decay. `dt` is the RENDER step, deliberately — see the header note.
  void Update(aether::F32 dt, bool raining);

  // How wet the soil is, in [0, 1] — what a soil tile's `MeshItem::wetness`
  // becomes.
  [[nodiscard]] aether::F32 Wetness() const;

  // Append the live streaks. Takes the camera because a 3D billboard has no
  // meaning without an eye.
  void Emit(std::vector<aether::Renderable>& out,
            const aether::Camera& camera) const;

  // Draw the background layer, from a RENDER PASS and nowhere else.
  //
  // It opens its own pass on the scene target with `clear = false`, which keeps
  // the depth the scene pass just wrote — that is what lets the draw's
  // depth-test-only state hide drops behind an island instead of painting them
  // over it. Compositing after the tonemap instead (what gpu_particle_lab does,
  // having no scene) would lose that and put display-referred streaks over a
  // resolved image.
  //
  // The camera comes from the frame the PASS was handed rather than being
  // stashed during extract, so nothing is written from one thread and read on
  // another when the loop is pipelined.
  //
  // Takes the framebuffer HANDLE, not `app::SceneTarget`: view/ links
  // aether::render and must not reach up into aether::app, and a handle is all
  // an opened pass needs.
  void SubmitBackground(aether::rhi::Device& device,
                        const aether::RenderView& view,
                        aether::FramebufferHandle target);

  [[nodiscard]] aether::Usize DropCount() const;
  // How many background drops are live, or 0 where there is no compute. The
  // debug HUD reads it, and it is an ESTIMATE by construction — a true count
  // needs a readback (`GpuParticleEmitter::LiveEstimate`).
  [[nodiscard]] aether::U32 SkyDropCount() const;

 private:
  // Where the rain box goes and how big its drops are, for one view. One place
  // rather than four scattered multiplications, because every one of them is a
  // fraction of the zoom and getting one wrong is invisible at the zoom you
  // happen to be testing at.
  struct SkyVolume {
    aether::Vec3 centre;
    aether::Vec3 half_extent;
    aether::F32 drop_size;
    aether::F32 fall_speed;
  };
  [[nodiscard]] static SkyVolume VolumeFor(const aether::Camera& camera);
  std::optional<aether::scene::SceneGeometryQuery3> query_;
  std::optional<aether::weather::RainField3> rain_;
  // NULL IS A SUPPORTED STATE, not a failure to handle: no compute, no sky
  // layer, and every other member goes on working.
  std::unique_ptr<aether::render::GpuParticleEmitter> sky_;
  bool sky_raining_ = false;
  aether::surface::WetnessField wetness_;
  std::vector<aether::weather::Impact3> impacts_;
  aether::resources::ResourceHandle<aether::resources::Texture> streak_;
  // The collider node the impacts report, and therefore the key wetness is
  // stored under. Captured at Create rather than looked up, because a lookup
  // by position is what lantern had to do and it is fragile.
  aether::U64 ground_surface_ = 0;
  // Open sky: the rain-shadow probe samples this and there is nothing on a farm
  // to shelter under. A constant field rather than a real sky-visibility bake,
  // because the answer everywhere is 1.
  aether::ValueField<aether::F32> open_sky_{1.0f};
};

}  // namespace hearthfield::view
