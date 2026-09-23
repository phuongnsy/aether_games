// The composition root: the app::Game that owns the scene and drives the sim.
//
// The ONLY layer that sees everything (AGENTS.md law 3), and the only one
// that may resolve a POINTER — picking needs a viewport, and the sim must not
// know what a viewport is. The clock is the same rule and arrives at H2.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "aether/app/app.hpp"
#include "aether/app/engine_spawners.hpp"
#include "aether/app/pbr_material_factory.hpp"
#include "aether/app/render_pipeline.hpp"
#include "aether/render/tonemap.hpp"
#include "aether/resources/world_chunk.hpp"
#include "aether/scene/world_instance.hpp"
#include "aether/scene_core/camera_component.hpp"
#include "aether/scene_core/orbit_component.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/ui/context.hpp"
#include "hf/content/camera.hpp"
#include "hf/features/economy/system.hpp"
#include "hf/features/livestock/system.hpp"
#include "hf/features/orders/system.hpp"
#include "hf/features/placement/system.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/features/production/system.hpp"
#include "hf/runtime/farms.hpp"
#include "hf/runtime/game_world.hpp"
#include "hf/runtime/grid.hpp"
#include "hf/view/board.hpp"
#include "hf/view/buildings.hpp"
#include "hf/view/camera_input.hpp"
#include "hf/view/camera_rig.hpp"
#include "hf/view/farm_audio.hpp"
#include "hf/view/hud.hpp"
#include "hf/view/steading.hpp"
#include "hf/view/weather_fx.hpp"
#include "screens.hpp"

namespace hearthfield::app {

// A chunk and what it spawned. BOTH, because a named lookup needs the chunk for
// the name and the instance for the node it became (scene::FindEntityNode), and
// view::Steading reads the mill, coop and yard that way.
//
// In the header rather than in the .cpp since sky world s5: the ZONE of an
// island being travelled to has to survive from departure — where it is loaded,
// under the flight — to arrival, where the farm's view is built from it.
struct LoadedChunk {
  aether::resources::WorldChunk chunk;
  aether::scene::WorldInstance instance;
};

class HearthfieldGame final : public aether::app::Game {
 public:
  HearthfieldGame(aether::U32 columns, bool sow_all, bool autopilot,
                  aether::I64 offline_override, bool no_save,
                  std::string open_screen = {}, bool force_rain = false,
                  bool start_building = false, int island_override = -1,
                  int travel_target = -1, bool ui_lint = false)
      : island_override_(island_override),
        travel_target_(travel_target),
        open_screen_(std::move(open_screen)),
        grid_{.columns = columns},
        world_(/*seed=*/1, runtime::Grid{.columns = columns}.Count()),
        sow_all_(sow_all),
        autopilot_(autopilot),
        offline_override_(offline_override),
        no_save_(no_save),
        force_rain_(force_rain),
        ui_lint_(ui_lint) {
    if (start_building) {
      build_kind_ = content::kMillKind;
    }
  }

  [[nodiscard]] aether::app::RenderPipeline BuildPipeline() override;
  // The lint itself. Needs a DEVICE (a font cannot be loaded without one) but
  // no display, so it runs under --headless on bgfx's Noop backend.
  [[nodiscard]] aether::Result<void> RunUiLint(
      const aether::app::AppContext& ctx);
  // The tab strip. EXTRACTED so the lint drives the same code the frame does —
  // a lint that builds its own arrangement of the UI checks an arrangement the
  // game never draws.
  void BuildTabs();
  aether::Result<void> Load(const aether::app::AppContext& ctx) override;
  aether::Result<aether::app::LoadProgress> LoadStep(
      const aether::app::AppContext& ctx) override;
  void Update(const aether::app::AppContext& ctx, aether::F32 dt) override;
  void FixedUpdate(const aether::app::AppContext& ctx, aether::F32 dt) override;
  aether::RenderFrame Extract(const aether::app::AppContext& ctx) override;
  aether::RenderFrame BuildOverlay(const aether::app::AppContext& ctx) override;
  void Unload(const aether::app::AppContext& ctx) override;

 private:
  void ResolvePointer(const aether::app::AppContext& ctx);

  // Where a screen point lands on the ground plane, or nothing when there is
  // no camera yet or the ray runs parallel to it. app/'s job because it is the
  // only layer that holds a viewport (hf/runtime/grid.hpp makes the same call
  // for picking).
  [[nodiscard]] std::optional<aether::Vec3> GroundAt(
      const aether::app::AppContext& ctx, aether::Vec2 pixels) const;
  [[nodiscard]] aether::U8 FillableSlot() const;
  // Load is best-effort by design: a first run has no save, and that is not an
  // error. A save that EXISTS and will not parse is, and says so.
  void LoadFarm(const aether::app::AppContext& ctx);
  void SaveFarm(const aether::app::AppContext& ctx);

  // Parse and spawn one chunk under `parent`. Five callers across Load, travel
  // and the neighbour silhouettes, which is why the six lines are written once.
  [[nodiscard]] aether::Result<LoadedChunk> LoadChunk(
      const aether::app::AppContext& ctx, std::string_view path,
      aether::scene::NodeId parent);

  // THE HEAVY HALF OF LOAD, one phase per call — see LoadPhase. Each returns
  // and the loop presents a loading frame between them, so the ordering these
  // carry is now enforced by the enum rather than by statement order.
  [[nodiscard]] aether::app::LoadProgress LoadingProgress() const;
  void LoadUiAssets(const aether::app::AppContext& ctx);
  aether::Result<void> LoadPersistent(const aether::app::AppContext& ctx);
  aether::Result<void> LoadIsland(const aether::app::AppContext& ctx);
  aether::Result<void> LoadFarmView(const aether::app::AppContext& ctx);
  aether::Result<void> LoadSystems(const aether::app::AppContext& ctx);

  // Spawn one island — its chunk, and its zone if it has one — under a fresh
  // node placed at that island's world position. Returns the node, so the
  // caller owns exactly one handle for the whole island.
  // `zone_out` receives the island's zone when it has one, so the caller can
  // build the farm's view from it — at ARRIVAL rather than here, because the
  // island being left still owns the board until then and there is only one
  // view::Board.
  [[nodiscard]] aether::Result<aether::scene::NodeId> SpawnIsland(
      const aether::app::AppContext& ctx, content::IslandId id,
      std::optional<LoadedChunk>* zone_out);
  // Build the farm's view — board, buildings, steading — from a zone that has
  // already been spawned. The one place that knows those three go together.
  [[nodiscard]] aether::Result<void> BuildFarmView(
      const aether::app::AppContext& ctx, const LoadedChunk& zone);
  // Rebuild the silhouettes: every island except the ones passed. Two, during a
  // crossing, because both ends of it are real geometry at that moment.
  aether::Result<void> RespawnNeighbours(content::IslandId resident,
                                         content::IslandId also_loaded);
  // Begin a crossing. Refuses a locked island, an unknown one, the island
  // already stood on, and a second crossing while one is in flight.
  aether::Result<void> BeginTravel(const aether::app::AppContext& ctx,
                                   content::IslandId target);
  // Advance a crossing; called once per rendered frame with the render dt.
  void StepTravel(const aether::app::AppContext& ctx, aether::F32 dt);

  aether::scene::Scene scene_;
  std::unique_ptr<aether::app::PbrMaterialFactory> materials_;
  std::unique_ptr<aether::app::EngineSpawners> spawners_;

  // --island: which island to load, overriding the save's. -1 means "the one
  // the farm was left standing on". It exists because s4 has no TRAVEL yet
  // (s5 does), so this is the only way to enter a satellite and therefore the
  // only way "playable" is a claim anyone can check. Declared FIRST because it
  // is initialised first.
  int island_override_ = -1;
  // --travel: begin a crossing at load, so one can be captured headless. -1 for
  // "stay put". Unlocks its target, which is why it is a dev flag and not a
  // verb (see Load).
  int travel_target_ = -1;

  // Whether the island being stood on has a ZONE — something built on it. False
  // on a satellite, and it gates every view that mirrors the farm: the board,
  // the buildings, the steading and the soil's wetness. The SIM is not gated,
  // because crops grow while you are elsewhere exactly as they grow while the
  // game is shut.
  bool has_zone_ = false;

  // --- the load, sliced across frames ---------------------------------------
  //
  // `Load` does only the cheap prologue — the save, which island, and the UI's
  // OWN assets, without which no loading screen could draw — and each phase
  // below is one heavy piece the loop calls between presented frames. Ordered
  // by the constraints Load's comments already carried: the persistent chunk
  // owns the camera, the island decides whether there is a farm to build, and
  // the systems come last because LoadFarm replaced `world_`.
  enum class LoadPhase : aether::U8 {
    kPersistent,
    kIsland,
    kNeighbours,
    kFarmView,
    kWeather,
    kSystems,
    kDone,
  };
  LoadPhase load_phase_ = LoadPhase::kPersistent;
  // The resident island's zone, carried from the phase that spawns it to the
  // one that builds the farm's view out of it. A local until 2026-08-30, which
  // is exactly what stopped the load being sliceable.
  std::optional<LoadedChunk> load_zone_;

  // --- the archipelago, and moving through it (sky world s5) ---------------
  //
  // ONE NODE PER ISLAND, with its chunk AND its zone parented under it, so an
  // unload is a single DestroyNode — the reason scene::WorldInstance carries a
  // wrapper root at all (GEA §16.4.1). The node also carries the island's world
  // position, which is what lets a chunk's entities stay authored in
  // island-local metres.
  aether::scene::NodeId island_root_;
  // The neighbours' silhouettes, rebuilt whenever the resident island changes
  // because the set is "every island except the one you are on".
  aether::scene::NodeId neighbours_root_;
  content::IslandId island_ = content::kHub;

  // A crossing in flight. GEA §16.4.2's air lock: the target island is loaded
  // when this begins and the departed one freed when it ends, so the flight is
  // what the load hides. Empty when the player is standing still.
  struct Transit {
    content::IslandId from = content::kHub;
    content::IslandId to = content::kHub;
    aether::F32 elapsed = 0.0f;
    // The island being LEFT, kept so it can be freed on arrival rather than at
    // departure — flying away from a hole in the sky is worse than paying for
    // two islands for two seconds.
    aether::scene::NodeId leaving;
    // The arriving island's zone, loaded at departure and held until the farm's
    // view can be built from it — which is on arrival, once the departed
    // island's board has been given back.
    std::optional<LoadedChunk> zone;
  };
  std::optional<Transit> transit_;

  // --screen: pushed once at load, so a capture of a panel needs no hand on
  // the mouse.
  std::string open_screen_;

  runtime::Grid grid_;
  runtime::GameWorld world_;
  // THE ISLANDS NOT BEING STOOD ON. `world_` is the live farm and always has
  // been; this holds the others as records, parked on departure and caught up
  // on arrival. Not one feature system knows it exists (the second farm's
  // design goal 2).
  runtime::Farms farms_;
  // Registration order is set in Load and it is the spec's (§5.1), not
  // alphabetical: plots, production, orders, economy.
  plots::PlotsSystem plots_;
  production::ProductionSystem production_;
  livestock::LivestockSystem livestock_;
  orders::OrdersSystem orders_;
  economy::EconomySystem economy_;
  placement::PlacementSystem placement_;
  view::Board board_;
  view::Buildings buildings_;
  view::Steading steading_;
  view::WeatherFx weather_;
  view::FarmAudio audio_;

  // BUILD MODE. `kNoBuilding` means the player is farming; anything else means
  // a ghost is following the pointer and the next tap places it. A mode rather
  // than a modifier because the tap means something different in it — and the
  // camera's tap is already arbitrated against dragging (ADR-0116), so this
  // needs no second drag test of its own.
  // Seeded by `--build`, which is the `--screen` precedent: a ghost needs a
  // hand on the mouse to appear, and a capture has none.
  content::BuildingKind build_kind_ = runtime::LatchedInput::kNoBuilding;
  runtime::BuildRing ring_;
  // WHERE THE GHOST SITS. The pointer moves it; it stays put when the pointer
  // is off the ring or absent entirely — which is the touch case, where there
  // is no pointer at all until a finger lands, so hiding it would mean build
  // mode showed nothing and left the player guessing. Starts beside the field
  // on the mill's side, clear of the mill, the coop and the crops.
  runtime::BuildCell build_cell_{.col = 13, .row = 10};
  // Borrowed from the AppContext at Load, because Extract and Unload both
  // want it and only Load is handed one that is guaranteed alive first.
  aether::audio::AudioSystem* audio_system_ = nullptr;

  // The in-game GUI (GEA §1.6.8.4): a pushdown stack of dismissable screens,
  // distinct from the HUD which is always on and lives in view/.
  aether::ui::Context ui_;
  std::shared_ptr<const aether::resources::Font> font_;
  // The 9-slice surfaces (ui_studio, `--style round`). OPTIONAL like the font:
  // a missing one costs the rounded frame and nothing else, and `Frame()` falls
  // back to the flat Panel it drew before.
  std::shared_ptr<const aether::resources::Texture> panel_tex_;
  std::shared_ptr<const aether::resources::Texture> btn_normal_;
  std::shared_ptr<const aether::resources::Texture> btn_hover_;
  std::shared_ptr<const aether::resources::Texture> btn_pressed_;
  ScreenStack screens_;

  // Built in Update from the pointer, consumed by the NEXT fixed step. `tap`
  // is latched rather than read live because FixedUpdate runs 0..N times per
  // frame and a tap read inside it is dropped on a zero-step frame — the
  // input-latch rule in AGENTS.md.
  runtime::LatchedInput latched_;

  aether::scene::NodeId camera_;
  aether::scene::OrbitComponent* orbit_ = nullptr;
  aether::scene::CameraComponent* projection_ = nullptr;

  // Borrowed by the pipeline, so both must outlive it. The 3D path writes HDR
  // and is crushed without a tonemap resolve — HdrPipeline is not an optional
  // effect (render_pipeline.hpp says so).
  aether::render::TonemapSettings tonemap_{};
  aether::U8 msaa_ = 4;

  // LAST FRAME's camera, because Scene::CameraParams is private and the only
  // public source of one is the frame Extract built. Picking against it is one
  // frame stale, which is what every immediate-mode picker does and is
  // imperceptible at a tap; `have_camera_` keeps frame 0 from picking against
  // a default-constructed camera looking down -Z from the origin.
  aether::Camera last_camera_{};
  bool have_camera_ = false;

  // Esc, consumed by the top screen. Not latched, because closing a panel is
  // FLOW rather than a game action — a replay reproduces what the farm did, not
  // which windows were open.
  bool close_requested_ = false;
  // Kept from Update so the screens can animate in Extract/BuildOverlay, which
  // are handed no dt of their own. Lantern does the same.
  aether::F32 last_dt_ = 1.0f / 60.0f;

  // THE BOARD CAMERA. Pan, anchored zoom and quarter-turn rotation live in
  // view/ across four single-purpose units (tuning data, gesture recognition,
  // device mapping, motion); app/ owns only the one thing that needs a
  // viewport — unprojecting the zoom anchor onto the ground.
  //
  // Four azimuths, one elevation (spec §1.1). Not a stylistic limit: a free
  // camera makes occlusion unbounded and every asset must read from every
  // angle, whereas four is a budget an art pass can meet. GEA §17.2.2 says the
  // same of the genre — an RTS camera pans, but its pitch and yaw are "usually
  // not under direct player control".
  content::CameraTuning camera_tuning_{};
  view::CameraRig rig_;
  std::optional<view::CameraInput> camera_input_;

  // Empty when there is nowhere to write: platform::UserDataDir REFUSES on web
  // and Android, and the game must still be playable — it simply does not
  // persist, and says so ONCE at load rather than failing at every autosave.
  std::string save_path_;
  // The digest last written. Autosave compares against it, so an idle farm
  // writes nothing at all — and the change test is one integer compare because
  // H0 built the digest for the replay oracle and it serves here unchanged.
  aether::U64 saved_digest_ = 0;
  aether::F32 since_save_ = 0.0f;

  bool sow_all_ =
      false;  // --sow: plant every plot at load, for the measurement
  bool autopilot_ = false;  // scripted taps, so --frames is reproducible
  // Advanced by the FIXED step, never by the frame — see FixedUpdate.
  aether::U32 script_beat_ = 0;
  // --offline SECONDS: pretend the game was shut this long. Check 4 is
  // otherwise a ten-minute wait, which is not a test anyone runs twice.
  aether::I64 offline_override_ = -1;
  bool no_save_ = false;  // --no-save: leave the player's real farm alone
  // --rain: force the sky on. A capture must not depend on which tick it
  // happened to start at, which is the same argument --sow and --offline make.
  bool force_rain_ = false;
  // Run the layout rules over every screen at every canvas size, then refuse to
  // start. A GATE, not a mode: it exits non-zero on a violation.
  bool ui_lint_ = false;
  aether::U32 frames_ = 0;
  aether::F64 gpu_ms_total_ = 0.0;
  aether::U32 gpu_samples_ = 0;
  aether::U32 draw_calls_ = 0;
  aether::U32 peak_draw_calls_ = 0;
  aether::U32 meshes_ = 0;
  aether::U32 visible_meshes_ = 0;
};

}  // namespace hearthfield::app
