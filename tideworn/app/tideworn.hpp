// The composition root: the app::Game that owns the scene and wires sim to
// presentation. The ONLY layer that sees everything (games/CLAUDE.md law 3).
#pragma once

#include "aether/app/app.hpp"
#include "aether/app/orbit_input.hpp"
#include "aether/app/render_pipeline.hpp"
#include "aether/render/skybox.hpp"
#include "aether/render/tonemap.hpp"
#include "aether/render/water_mesh.hpp"
#include "aether/resources/environment.hpp"
#include "aether/resources/model.hpp"
#include "aether/scene/skinned_mesh_component.hpp"
#include "aether/scene_core/orbit_component.hpp"
#include "aether/scene_core/punctual_light_component.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/ui/context.hpp"
#include "swim_camera.hpp"
#include "tideworn/runtime/game_world.hpp"
#include "tideworn/view/creatures.hpp"
#include "tideworn/view/disturbance.hpp"
#include "tideworn/view/rain.hpp"
#include "tideworn/view/sky_ring.hpp"
#include "tideworn/view/spray.hpp"

namespace tideworn::app {

// Everything the command line decides — one struct, because the list keeps
// growing and positional constructor arguments were already ambiguous.
struct Options {
  aether::F32 time_scale = 1.0f;
  aether::F32 pitch = 0.22f;
  aether::F32 yaw = 0.4f;
  aether::F32 distance = 14.0f;
  bool swim = false;               // boot in the first-person swim camera
  aether::F32 start_clock = 0.0f;  // pre-run the voyage this many seconds
  // Where the SWIM camera starts, so the diving view can be aimed from the
  // CLI. An art pass on water needs to look up at the surface, level into the
  // fog and down into the deep — three different images from one scene.
  aether::F32 depth = 2.0f;  // metres below the surface (positive down)
};

class TidewornGame final : public aether::app::Game {
 public:
  explicit TidewornGame(const Options& options) : options_(options) {}

  [[nodiscard]] aether::app::RenderPipeline BuildPipeline() override;
  [[nodiscard]] aether::Result<void> Load(
      const aether::app::AppContext& ctx) override;
  void Update(const aether::app::AppContext& ctx, aether::F32 dt) override;
  void FixedUpdate(const aether::app::AppContext& ctx, aether::F32 dt) override;
  [[nodiscard]] aether::RenderFrame Extract(
      const aether::app::AppContext& ctx) override;
  void Unload(const aether::app::AppContext& ctx) override;

 private:
  void DrawSky(const aether::app::FrameContext& c);
  void DrainEvents(aether::F32 clock_s);
  void StepDisturbance(const runtime::ViewSnapshot& snapshot);

  runtime::GameWorld world_;
  aether::scene::Scene scene_;
  aether::ui::Context ui_;  // gates OrbitInput; becomes the HUD later
  aether::app::OrbitInput orbit_input_;
  aether::scene::OrbitComponent* orbit_ = nullptr;
  aether::render::Skybox skybox_;
  aether::render::WaterMesh water_mesh_;
  // The opaque scene's colour copy (ADR-0058): the water samples what stands
  // behind it, so refraction bends the image; the underwater passes read the
  // same copy. A copy because a pass cannot sample the target it writes.
  aether::app::SceneTarget scene_copy_;
  view::Spray spray_;
  view::Rain rain_;
  view::SkyRing sky_ring_;
  view::Disturbance disturbance_;
  aether::TextureHandle disturbance_texture_;
  // Stamps rain splashes at rate-matched random positions (see view::Rain);
  // fixed seed so captures reproduce.
  std::mt19937 splash_rng_{4111};
  aether::F32 splash_accum_ = 0.0f;
  aether::F32 last_grid_clock_ = -1.0f;
  // Per-frame presentation data the PASSES read but EXTRACT writes, RUNG per
  // the engine/app recipe (Extract writes slot k % N, a pass reads
  // FrameContext::frame_index % N) — safe in serial and pipelined modes.
  // Spray must draw after the water pass; the sky pass needs the frame's two
  // keyframe cubes + blend.
  struct FrameFx {
    std::vector<aether::Renderable> spray;
    aether::TextureHandle sky_a;
    aether::TextureHandle sky_b;
    aether::F32 sky_blend = 0.0f;
    aether::F32 sky_flash = 0.0f;  // lightning: skybox intensity spike
    // The camera sits below the wave surface: the water pass becomes the
    // underwater fog/ceiling, and the airborne list is skipped.
    bool underwater = false;
    // Above the water but close enough that the surface can cross the near
    // plane: the mesh cannot rasterise behind it, so the underwater pass runs
    // first as a straddle fill and the mesh overdraws what it can see.
    bool near_surface = false;
    // The disturbance grid's encoded gradient + region for this frame's
    // upload — pixels rung here because Device::UpdateTexture is a pass-side
    // (API-thread) call while the sim steps in Extract.
    std::vector<aether::U8> grid_pixels;
    aether::Vec4 grid_region{0.0f, 0.0f, 0.0f, 0.0f};
  };
  static constexpr aether::Usize kFxRing = 3;
  std::array<FrameFx, kFxRing> fx_ring_;
  aether::U64 extract_count_ = 0;
  aether::render::TonemapSettings tonemap_;
  aether::resources::ResourceHandle<aether::resources::Environment>
      environment_;  // single-bake fallback when the ring fails to load
  aether::resources::ResourceHandle<aether::resources::Model> hull_;
  aether::scene::PunctualLightComponent* sun_ = nullptr;
  // Lightning's own directional light — never the sun's (the sky ring owns
  // that), and created AFTER it: the water derives its sun from the FIRST
  // directional in the frame, so the order is load-bearing.
  aether::scene::PunctualLightComponent* flash_ = nullptr;
  // Recent strikes as (voyage clock, azimuth); the flash envelope decays on
  // the same clock the strike was scheduled on, so captures reproduce.
  std::vector<std::pair<aether::F32, aether::F32>> strikes_;
  aether::scene::NodeId boat_;
  aether::scene::NodeId camera_;
  // The third-person body: a skinned diver the follow camera looks at. The
  // component pointer drives swim/idle cross-fades from the diver's speed.
  aether::scene::NodeId diver_;
  aether::scene::SkinnedMeshComponent* diver_mesh_ = nullptr;
  aether::resources::ResourceHandle<aether::resources::Model> diver_model_;
  bool diver_swimming_ = false;
  view::Creatures creatures_;
  Options options_;
  // The camera MODE: V toggles between the boat orbit (sailing) and the
  // first-person swim camera (diving). GEA §17.2.2: two camera types.
  bool swim_ = false;
  SwimCamera swim_camera_;
  bool was_underwater_ = false;  // the waterline hysteresis latch
};

}  // namespace tideworn::app
