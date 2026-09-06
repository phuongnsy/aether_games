// Coin Rush — the weather platformer's orchestrator. Declares the game class so
// screens.cpp and main.cpp share it; method bodies live in coin_rush.cpp.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/app/app.hpp"
#include "aether/audio/audio_system.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/core/types.hpp"
#include "aether/input/action_map.hpp"
#include "aether/resources/animation_set.hpp"
#include "aether/resources/audio_clip.hpp"
#include "aether/resources/effect_resource.hpp"
#include "aether/resources/font.hpp"
#include "aether/resources/resource_handle.hpp"
#include "aether/resources/texture.hpp"
#include "aether/scene/sprite_component.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/ui/context.hpp"
#include "aether/ui/screen.hpp"
#include "aether/ui/transition.hpp"
#include "cr/content/levels.hpp"
#include "cr/features/coins/system.hpp"
#include "cr/features/movement/system.hpp"
#include "cr/features/progression/system.hpp"
#include "cr/features/weather/system.hpp"
#include "cr/runtime/game_world.hpp"
#include "cr/runtime/replay.hpp"
#include "cr/view/effects.hpp"
#include "cr/view/renderer.hpp"
#include "cr/view/scene_materials.hpp"
#include "game_api.hpp"
#include "input_source.hpp"

namespace game {

// The game layer works in aether's vocabulary (as every game TU does); this
// header is included only by the game's own TUs, so the directive stays local.
using namespace aether;  // NOLINT(google-build-using-namespace)

class CoinRushGame;

// --- the game ---------------------------------------------------------------

// Implements GameApi so the flow screens drive the game with no friendship.
// Weather is a normal sim slice now (no worldsim::Coordinator bridge).
class CoinRushGame final : public app::Game, public GameApi, public InputWorld {
 public:
  // Harness input record/playback (env vars in main): replay_path plays a run
  // back; record_path writes the run's inputs on teardown. Empty = off.
  // `shader_dir` is where this build's compiled shaders live. A PARAMETER and
  // not the AETHER_SHADER_DIR macro it defaults to, because that macro is the
  // BUILD MACHINE's absolute path — meaningless inside an APK, which is where
  // the Android entry point passes the on-device location instead
  // (docs/plans/2026-08-22-android-tier2-package.md t4).
  explicit CoinRushGame(int start_level = 0, std::string record_path = {},
                        std::string replay_path = {},
                        std::string shader_dir = {})
      : record_path_(std::move(record_path)),
        replay_path_(std::move(replay_path)),
        shader_dir_(std::move(shader_dir)),
        current_level_(std::clamp(start_level, 0, LevelCount() - 1)) {}

  // Writes the recorded run to record_path_ (if set) before members tear down.
  ~CoinRushGame() override;

  Result<void> Load(const app::AppContext& ctx) override;
  void Update(const app::AppContext& ctx, F32 dt) override;
  void FixedUpdate(const app::AppContext& ctx, F32 dt) override;
  RenderFrame Extract(const app::AppContext& ctx) override;
  RenderFrame BuildOverlay(const app::AppContext& ctx) override;
  app::RenderPipeline BuildPipeline() override;
  void PostProcess(const app::AppContext& ctx, TextureHandle scene_color);

  // No Unload override: App::Run destroys this Game while the device is still
  // alive, so every resource member frees itself in order via RAII.

 private:
  Result<void> LoadRenderAssets(const app::AppContext& ctx);

  [[nodiscard]] std::span<const std::string_view> CurrentMap() const {
    return LevelMap(current_level_);
  }
  [[nodiscard]] WeatherConfig CurrentWeather() const {
    return LevelWeather(current_level_);
  }

  void BuildArena();
  void AddTile(const resources::ResourceHandle<resources::Texture>& tex,
               Vec3 pos, U16 layer);
  void AddWater(Vec3 pos);
  void AddExit(Vec3 pos);
  void AddWall(Vec3 pos);
  void SpawnPickupPop(Vec3 pos);
  void AddCoin(Vec3 pos);
  void UpdateCoinGlow();
  void SetupAudio(const app::AppContext& ctx);

  // Drain one sim GameEvent into presentation/flow (particles, audio, camera,
  // screen flow). The single reaction router (see coin_rush.cpp).
  void ApplyEvent(const GameEvent& e);

  // Kick a full-screen feedback flash (win/lose payoff), at the UI layer.
  void TriggerFlash(Vec4 color, F32 strength) {
    flash_ = strength;
    flash_color_ = color;
  }

  void StartRound() override;         // GameApi
  void FrameEstablishing() override;  // GameApi
  void UpdatePlayerAnimation(Vec2 dir);
  Vec2 AdvancePlayer(const app::AppContext& ctx, F32 dt,
                     const LatchedInput& in);
  // Pick the frame's input source once, from the run mode: replay file → live
  // keyboard → capture autopilot (see input_source.hpp).
  [[nodiscard]] std::unique_ptr<InputSource> MakeInputSource(
      const app::AppContext& ctx);
  void StepWorld(const app::AppContext& ctx, F32 dt) override;  // GameApi
  void SpawnTrailGhost();
  Vec2 ReadMove(const app::AppContext& ctx);
  Vec2 DragMove(const app::AppContext& ctx);
  TextureHandle MakeSoftDot(const app::AppContext& ctx);
  void DrawJoystick(ui::Context& ui) override;                     // GameApi
  void DrawJumpButton(ui::Context& ui) override;                   // GameApi
  void DrawPanel(ui::Context& ui, const ui::Rect& rect) override;  // GameApi

  // --- the rest of GameApi (impls in coin_rush.cpp; the screens' only door)
  // ---
  void StartLevel(int index) override;
  void NextLevel() override;
  void ToMenu() override;

  // --- InputWorld: the narrow facts the live/autopilot input sources read ---
  [[nodiscard]] bool PlayerOnGround() const override {
    return movement_.OnGround();
  }
  [[nodiscard]] Vec2 SteerFromDrag(const app::AppContext& ctx) override {
    return ReadMove(ctx);
  }
  [[nodiscard]] bool TakeJumpPress(const app::AppContext& ctx) override;
  [[nodiscard]] bool JumpHeld(const app::AppContext& ctx) const override;
  [[nodiscard]] const resources::Font* TitleFont() const override {
    return title_font_.get();
  }
  [[nodiscard]] PlayHud HudState() const override;
  [[nodiscard]] bool TouchControlsVisible() const override {
    return touch_bound_;
  }
  [[nodiscard]] bool TakeSuspendPause() override {
    return std::exchange(suspend_pause_, false);
  }
  [[nodiscard]] bool HasNextLevel() const override {
    return current_level_ + 1 < LevelCount();
  }
  [[nodiscard]] RoundResult RoundSummary() const override;

  // Switch screens behind a full-screen fade (the swap happens hidden at full
  // cover). Used for every hard scene change (menu↔play↔result).
  void TransitionTo(std::unique_ptr<GameScreen> next);
  [[nodiscard]] bool Transitioning() const { return transition_.Active(); }

  // Clip index (AnimatorComponent state) for a clip name, or 0 if absent.
  [[nodiscard]] int ClipState(const std::string& name) const {
    const auto it = clip_state_.find(name);
    return it == clip_state_.end() ? 0 : it->second;
  }

  scene::Scene scene_;

  F32 reference_size_ = 0.0f;  // ctx.reference_size (see Load)
#ifdef AETHER_DEV_TOOLS
#endif
  resources::ResourceHandle<resources::Texture> coin_tex_;
  resources::ResourceHandle<resources::Texture> wall_tex_;
  resources::ResourceHandle<resources::Texture> floor_tex_;
  resources::ResourceHandle<resources::Texture>
      panel_tex_;  // ui_studio 9-slice
  resources::ResourceHandle<resources::Texture> btn_normal_;  // button states
  resources::ResourceHandle<resources::Texture> btn_hover_;
  resources::ResourceHandle<resources::Texture> btn_pressed_;
  TextureHandle particle_tex_;  // raw handle (owned; freed by bgfx::shutdown)
  std::shared_ptr<const resources::Font> font_;        // HUD / body text
  std::shared_ptr<const resources::Font> title_font_;  // headings
  TextureHandle white_;

  scene::NodeId player_;
  scene::NodeId camera_;
  // vfx_studio effects: the loaded resources (own the texture pages) + their
  // per-round runtime emitters (rebuilt by BuildArena).
  resources::ResourceHandle<resources::EffectResource> coin_fx_;
  resources::ResourceHandle<resources::EffectResource> walk_dust_fx_;
  resources::ResourceHandle<resources::EffectResource> wall_sparks_fx_;
  resources::ResourceHandle<resources::EffectResource> goal_fx_;
  EffectRuntime coin_rt_;
  EffectRuntime walk_dust_rt_;
  EffectRuntime wall_sparks_rt_;
  EffectRuntime goal_rt_;

  ui::Context ui_;
  GameStack screens_;
  ui::Transition transition_;  // full-screen fade between screens

  // Presentation lives in cr_view: the post/lighting/bloom pipeline (Renderer)
  // + the per-object materials (SceneMaterials); the app just owns the units.
  Renderer renderer_;
  SceneMaterials scene_materials_;
  std::vector<scene::NodeId> coins_;  // live coins, for the spotlight glow
  // Parallax starfield background (P3): the bg texture + a big quad parented to
  // the camera node (the quad's scrolling material lives in scene_materials_).
  resources::ResourceHandle<resources::Texture> bg_tex_;
  scene::NodeId bg_node_;
  // Water pool (P3): a hand-animated ocean sprite sheet (4 frames), played per
  // tile by an AnimatorComponent — real wave frames, not a UV distortion.
  resources::ResourceHandle<resources::Texture> ocean_tex_;
  // Player animation (P2): the animation_studio set + a name→state (clip index)
  // map + the last movement facing (so idle faces the last-walked way).
  resources::ResourceHandle<resources::AnimationSet> player_anim_;
  std::unordered_map<std::string, int> clip_state_;
  std::string last_facing_ = "down";
  F32 trail_timer_ = 0.0f;       // throttles the motion-trail afterimages
  F32 dust_timer_ = 0.0f;        // throttles walk_dust puffs while moving
  F32 wall_spark_timer_ = 0.0f;  // cooldown between wall_hit_sparks bursts
  F32 elapsed_ = 0.0f;  // drives the glow pulse + floor scroll + bg drift
  F32 day_ = 0.0f;      // P7 daylight factor 0..1 (0 night, 1 noon)
  // P7 lighting ambient (animated by day_); starts at the lit-dusk kAmbient.
  Vec3 day_ambient_{0.40f, 0.42f, 0.55f};

  // The deterministic fixed-step core + its feature slices (registration order
  // = step order); world_.Step runs them, emitting events the view drains.
  GameWorld world_;
  // Registration order = step order. Weather runs FIRST so the wetness it
  // writes is fresh when movement reads it (see Load).
  WeatherSystem weather_;           // 0: wind + rain/snow → wetness/accum
  MovementSystem movement_;         // 1: kinematics
  CoinsSystem coins_system_;        // 2: pickup detection (after movement)
  ProgressSystem progress_system_;  // 3: timer + unlock + win (after coins)
  // The frame's input source (live/autopilot/replay) + a recorder; the run's
  // inputs go to record_path_ on teardown.
  std::unique_ptr<InputSource> input_;
  ReplayRecorder recorder_;
  std::string record_path_;
  std::string replay_path_;
  std::string shader_dir_;
  scene::SpriteComponent* exit_sprite_ = nullptr;  // brightens when unlocked
  int current_level_ = 0;                          // index into the level set

  // P5 feedback/juice state (decayed in Extract, drawn by the screens/overlay).
  F32 score_punch_ = 0.0f;  // 1→0 after a pickup: brightens the HUD score +
                            // drives the rising "+1" popup
  F32 flash_ = 0.0f;        // full-screen feedback flash intensity, 1→0
  Vec4 flash_color_{1.0f, 1.0f, 1.0f, 1.0f};

  resources::ResourceHandle<resources::AudioClip> coin_clip_;
  audio::AudioSystem* audio_ = nullptr;

  bool dragging_ = false;
  Vec2 drag_anchor_{0.0f, 0.0f};
  Vec2 drag_pos_{0.0f, 0.0f};

  // Keys AND the on-screen button reach jump through here, so neither the
  // keyboard path nor the touch path is the special case.
  input::ActionMap actions_;
  // Latched in Update for DrawJumpButton, which (like DrawJoystick) is handed
  // only the ui context and reads presentation state off the game.
  bool pointer_on_jump_ = false;
  // Set once a touch has been seen; gates the jump button's art AND its binding
  // together, so what cannot be seen cannot be pressed.
  bool touch_bound_ = false;
  // Latched from app::AppSuspended, consumed by the play screen.
  bool suspend_pause_ = false;
};

}  // namespace game
