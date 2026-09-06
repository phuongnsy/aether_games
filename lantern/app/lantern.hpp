// The composition root: the app::Game that owns the scene and drives the sim.
//
// It is the ONLY layer that sees everything (games/CLAUDE.md law 3). The world
// is instantiated here because that needs app::EngineSpawners and a material
// factory — both above the sim layers — and the Scene is then handed to the
// fixed step by reference, which is what keeps `runtime` render-free.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aether/app/app.hpp"
#include "aether/app/engine_spawners.hpp"
#include "aether/app/orbit_input.hpp"
#include "aether/app/pbr_material_factory.hpp"
#include "aether/app/render_pipeline.hpp"
#include "aether/audio/spatial.hpp"
#include "aether/resources/audio_clip.hpp"
#include "aether/resources/font.hpp"
#include "aether/resources/model.hpp"
#include "aether/scene/skinned_mesh_component.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/ui/context.hpp"
#include "lantern/features/weather/weather.hpp"
#include "lantern/runtime/game_world.hpp"
#include "lantern/view/lights.hpp"
#include "lantern/view/weather_fx.hpp"

namespace lantern::app {

class LanternGame final : public aether::app::Game {
 public:
  LanternGame(bool demo, int lit, weather::Mode weather_mode, bool soft,
              aether::F32 soft_fade)
      : demo_(demo),
        lit_at_load_(lit),
        weather_mode_(weather_mode),
        soft_particles_(soft),
        soft_fade_(soft_fade) {}

  [[nodiscard]] aether::app::RenderPipeline BuildPipeline() override;
  aether::Result<void> Load(const aether::app::AppContext& ctx) override;
  void Update(const aether::app::AppContext& ctx, aether::F32 dt) override;
  void FixedUpdate(const aether::app::AppContext& ctx, aether::F32 dt) override;
  aether::RenderFrame Extract(const aether::app::AppContext& ctx) override;
  aether::RenderFrame BuildOverlay(const aether::app::AppContext& ctx) override;
  void Unload(const aether::app::AppContext& ctx) override;

 private:
  void DrainEvents();
  void DriveScript(aether::F32 dt);
  void PlaceCamera(const aether::app::AppContext& ctx);
  void DriveClip();

  aether::scene::Scene scene_;
  aether::ui::Context ui_;
  std::unique_ptr<aether::app::PbrMaterialFactory> materials_;
  std::unique_ptr<aether::app::EngineSpawners> spawners_;
  std::shared_ptr<const aether::resources::Font> font_;
  aether::resources::ResourceHandle<aether::resources::Model> character_;
  aether::resources::ResourceHandle<aether::resources::AudioClip> chime_;

  runtime::GameWorld world_;
  runtime::Input input_;
  view::Lights lights_;
  view::WeatherFx weather_fx_;
  // Each platform's collider node id (what the impacts report) paired with
  // the MESH node whose material shows the wetness.
  std::vector<std::pair<aether::U64, aether::scene::NodeId>> wet_surfaces_;

  // The camera is ORBITED by the player, not bolted behind them. A fixed offset
  // meant that standing behind a tier put the camera inside it, and the
  // collision pull-in then slammed it into the walker's back — the view was
  // lost exactly when something interesting was happening, and there was no
  // way to look around it.
  aether::F32 cam_yaw_ = 0.0f;
  aether::F32 cam_pitch_ = 0.35f;
  aether::F32 cam_distance_ = 8.0f;
  aether::audio::AudioSystem* audio_ = nullptr;
  aether::scene::NodeId camera_;
  aether::scene::NodeId body_;
  aether::scene::SkinnedMeshComponent* skinned_ = nullptr;
  std::string clip_;
  // Kept from Update so the camera can settle framerate-independently in
  // Extract, which is handed no dt of its own.
  aether::F32 last_dt_ = 1.0f / 60.0f;
  // --demo walks the intended route on a FIXED step, so `--frames N` means the
  // same thing on every machine and a capture of check 4 is reproducible
  // rather than hand-played (the wall-clock trap thaw_lab fell into).
  bool demo_ = false;
  // --lit N lights N lanterns at load so the shadow cost is a reproducible
  // command-line A/B rather than a hand-played one.
  int lit_at_load_ = 0;
  weather::Mode weather_mode_ = weather::Mode::kOff;
  // Soft particles ON by default here, unlike the lab: this camera sits ~8 m
  // from the spire rather than 26 m out, so rain is genuinely near what it is
  // seen against — MEASURED at 2088 changed pixels against the lab's 11, for
  // +31 us GPU and 9 draws. docs/plans/2026-08-14-soft-particles.md §6.
  bool soft_particles_ = true;
  aether::F32 soft_fade_ = 1.0f;
  aether::F64 gpu_ms_total_ = 0.0;
  aether::U32 gpu_samples_ = 0;
  aether::U32 draw_calls_ = 0;
  aether::F32 script_time_ = 0.0f;
  aether::Usize script_step_ = 0;
};

}  // namespace lantern::app
