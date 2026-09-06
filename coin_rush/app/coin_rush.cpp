// Coin Rush — the M8 vertical slice: a collect-'em-up integrating M1–M7 (follow
// cam, culling, particles, audio, UI/screens, events, collision queries).
#include "coin_rush.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aether/app/app.hpp"
#include "aether/app/app_events.hpp"
#include "aether/app/render_pipeline.hpp"
#include "aether/audio/audio_system.hpp"
#include "aether/core/error.hpp"
#include "aether/core/log.hpp"
#include "aether/core/math/geometry.hpp"
#include "aether/core/math/math.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/core/types.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/platform/key.hpp"
#include "aether/platform/window.hpp"
#include "aether/render/renderer.hpp"
#include "aether/resources/animation_set.hpp"
#include "aether/resources/audio_clip.hpp"
#include "aether/resources/effect_resource.hpp"
#include "aether/resources/font.hpp"
#include "aether/resources/resource_manager.hpp"
#include "aether/resources/texture.hpp"
#include "aether/rhi/device.hpp"
#include "aether/scene/actions.hpp"
#include "aether/scene/animator_component.hpp"
#include "aether/scene/sprite_component.hpp"
#include "aether/scene_core/camera_component.hpp"
#include "aether/scene_core/collider_component.hpp"
#include "aether/scene_core/follow_component.hpp"
#include "aether/scene_core/light_component.hpp"
#include "aether/scene_core/push_component.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/scene_core/shake_component.hpp"
#include "aether/ui/context.hpp"
#include "aether/ui/screen.hpp"
#include "aether/ui/transition.hpp"
#include "assets.hpp"
#include "cr/content/levels.hpp"
#include "cr/features/movement/system.hpp"
#include "cr/view/effects.hpp"
#include "cr/view/hud.hpp"
#include "cr/view/theme.hpp"
#include "screens.hpp"
#include "tuning.hpp"

#ifndef AETHER_SHADER_DIR
#define AETHER_SHADER_DIR "."
#endif
#ifndef AETHER_ASSET_DIR
#define AETHER_ASSET_DIR "."
#endif

using namespace aether;
using namespace game;  // game-local: theme/palette (and later units)

namespace {

// --- tuning (shared constants: tuning.hpp; kPlayerRadius in player.hpp) ------
constexpr F32 kPlayZoom = 2.7f;   // play framing (see camera reference_size)
constexpr F32 kMapZoom = 1.15f;   // establishing shot: whole arena visible
constexpr F32 kIntroZoom = 0.7f;  // seconds to ease kMapZoom → kPlayZoom
constexpr F32 kPushMax = 22.0f;   // camera nudge when pressing into a wall
constexpr F32 kFadeTime = 0.45f;  // full-screen fade between screens
constexpr F32 kDragDeadzone = 12.0f;
constexpr F32 kDragRadius = 140.0f;

// The game's actions. Only jump is bound: steering is an axis and belongs
// nowhere near a digital binding (GEA §9.5.7's control classes).
enum Action : U32 { kActionJump = 0 };

// The touch jump button, NORMALIZED to the framebuffer — bottom-right, under
// the right thumb. One rect for both the art and the action binding, so they
// cannot drift apart.
//
// Fractions of each axis rather than a pixel size: a fixed size is a postage
// stamp on a dense phone and a slab on a tablet. The cost is that it is not
// square off 16:9, which for a thumb target does not matter.
constexpr Rect kJumpButton{
    .x = 0.83f, .y = 0.70f, .width = 0.13f, .height = 0.22f};

constexpr F32 kDustInterval = 0.16f;  // walk_dust puff cadence while moving
constexpr F32 kWallSparkGap = 0.22f;  // min seconds between wall-hit sparks

// kJumpButton in framebuffer pixels — the one conversion, shared by the art and
// by the hit test that keeps a jump tap from also steering.
[[nodiscard]] Rect JumpButtonPixels(Size framebuffer) {
  const auto w = static_cast<F32>(framebuffer.width);
  const auto h = static_cast<F32>(framebuffer.height);
  return Rect{.x = kJumpButton.x * w,
              .y = kJumpButton.y * h,
              .width = kJumpButton.width * w,
              .height = kJumpButton.height * h};
}

// P6 lighting: a lit dusk (not pitch black) that stays playable; torch + coin
// glow are the accents. day_ lifts it toward kDayAmbient over kDayCycle.
constexpr Vec3 kAmbient{0.40f, 0.42f, 0.55f};     // lit dusk (weather-mood)
constexpr Vec3 kDayAmbient{0.80f, 0.82f, 0.90f};  // bright cool daylight
constexpr F32 kDayCycle = 24.0f;  // seconds per full night→day→night loop
// Torch spotlight: coins within kSpotRange ramp emissive so bloom reveals them;
// kTorchRadius reaches past that range so wall shadows read as dark cutouts.
constexpr F32 kSpotRange = kTile * 5.0f;
constexpr F32 kTorchRadius = kTile * 5.0f;
constexpr F32 kCoinDim = 0.75f;    // sprite brightness far from the torch
constexpr F32 kCoinBright = 2.3f;  // sprite brightness under the torch (blooms)

// Parallax starfield backdrop (P3): a big quad parented to the camera, lowest
// layer; its UVs tile and drift with camera motion plus a slow ambient scroll.
constexpr Vec2 kBgSize{2200.0f,
                       1400.0f};      // world units — covers any camera pos
constexpr F32 kBgTileWorld = 140.0f;  // world units per starfield tile (bg UV)

// 9-slice margins of the ui_studio panel + button textures (their corner
// radii).
constexpr F32 kPanelMargin = 8.0f;
constexpr F32 kButtonMargin = 8.0f;

// Level data (maps, names, per-level WeatherConfig) lives in level.hpp/.cpp.

// (Coin/exit/win reactions flow through the runtime GameEvent bus, routed by
// CoinRushGame::ApplyEvent — no per-game event structs on the engine bus.)

// (The collision + weather adapters now live in the movement feature, backed by
// the runtime WorldView — see features/movement/src/system.cpp.)

}  // namespace

namespace game {

// --- CoinRushGame method definitions (class declared in coin_rush.hpp) ------
Result<void> CoinRushGame::Load(const app::AppContext& ctx) {
  reference_size_ = ctx.reference_size;  // re-applied per arena (fresh Scene)
  // Jump's bindings: two keys and the on-screen button, all equal citizens.
  actions_.Bind(kActionJump, platform::Key::kSpace);
  actions_.Bind(kActionJump, platform::Key::kUp);
  // The jump REGION is bound on first touch, not here — see Update. A device
  // with a keyboard never binds it, so the button is neither drawn nor
  // pressable there: an invisible hitbox in a desktop corner is a bug, and
  // it would move every capture digest for a control nobody can see.
  //
  // Backgrounding the app comes back PAUSED. Only latched here; the play screen
  // consumes it, since a suspend on the menu must not stack a pause over it.
  ctx.events.Subscribe<app::AppSuspended>(
      [this](const app::AppSuspended&) { suspend_pause_ = true; });
  auto load_tex =
      [&](std::string_view path,
          resources::ResourceHandle<resources::Texture>& dst) -> Result<void> {
    auto t = ctx.resources.Load<resources::Texture>(path);
    if (!t) {
      return std::unexpected(t.error());
    }
    dst = *t;
    return {};
  };
  // The player's animation set (animation_studio): its page texture + named
  // clips. Build the name→state map the AnimatorComponent is keyed on.
  auto set = ctx.resources.Load<resources::AnimationSet>(
      assets::animations::kPlayerAnim);
  if (!set) {
    return std::unexpected(set.error());
  }
  player_anim_ = *set;
  for (Usize i = 0; i < player_anim_->Clips().size(); ++i) {
    clip_state_[player_anim_->Clips()[i].name] = static_cast<int>(i);
  }
  if (auto r = load_tex(assets::textures::kCoin, coin_tex_); !r) {
    return r;
  }
  if (auto r = load_tex(assets::textures::kWall, wall_tex_); !r) {
    return r;
  }
  if (auto r = load_tex(assets::textures::kFloor, floor_tex_); !r) {
    return r;
  }
  if (auto r = load_tex(assets::textures::kStarfield, bg_tex_); !r) {
    return r;
  }
  if (auto r = load_tex(assets::textures::kOcean, ocean_tex_); !r) {
    return r;
  }
  if (auto r = load_tex(assets::textures::kPanelPng, panel_tex_); !r) {
    return r;
  }
  if (auto r = load_tex(assets::textures::kBtnNormal, btn_normal_); !r) {
    return r;
  }
  if (auto r = load_tex(assets::textures::kBtnHover, btn_hover_); !r) {
    return r;
  }
  if (auto r = load_tex(assets::textures::kBtnPressed, btn_pressed_); !r) {
    return r;
  }

  // A pixel bitmap font generated by ui_studio (BMFont .fnt + atlas). White
  // glyphs, tinted per-label by the renderer.
  auto hud_font = ctx.resources.Load<resources::Font>(assets::fonts::kHud);
  if (!hud_font) {
    return std::unexpected(hud_font.error());
  }
  font_ = *hud_font;
  // A larger display font for headings (the body/title distinction).
  auto title = ctx.resources.Load<resources::Font>(assets::fonts::kTitle);
  if (!title) {
    return std::unexpected(title.error());
  }
  title_font_ = *title;

  // Held as members so their particle-texture pages stay alive; BuildArena
  // turns each into a burst-only emitter, fired via SpawnAt at its game event.
  using FxSlot =
      std::pair<std::string_view,
                resources::ResourceHandle<resources::EffectResource>*>;
  const std::array<FxSlot, 4> fx_assets = {
      FxSlot{assets::effects::kCoinPickup, &coin_fx_},
      FxSlot{assets::effects::kWalkDust, &walk_dust_fx_},
      FxSlot{assets::effects::kWallHitSparks, &wall_sparks_fx_},
      FxSlot{assets::effects::kGoalExplosion, &goal_fx_},
  };
  for (const auto& [path, out] : fx_assets) {
    auto fx = ctx.resources.Load<resources::EffectResource>(path);
    if (!fx) {
      return std::unexpected(fx.error());
    }
    *out = *fx;
  }

  particle_tex_ = MakeSoftDot(ctx);
  const U32 white_pixel = 0xffffffff;
  if (auto w = ctx.device.CreateTexture(1, 1, rhi::TextureFormat::kRGBA8,
                                        &white_pixel, sizeof(white_pixel))) {
    white_ = *w;
  }

  ui_.SetTheme(ui::Theme{.panel = kUiPanel,
                         .button = kUiButton,
                         .button_hover = kUiButtonHover,
                         .button_active = kUiButtonActive,
                         .text = kUiText});
  // Textured 9-slice buttons (ui_studio) with per-state art.
  ui_.SetButtonSkin(ui::ButtonSkin{.normal = btn_normal_->Handle(),
                                   .hover = btn_hover_->Handle(),
                                   .pressed = btn_pressed_->Handle(),
                                   .tex_width = btn_normal_->Width(),
                                   .tex_height = btn_normal_->Height(),
                                   .margins = {.left = kButtonMargin,
                                               .right = kButtonMargin,
                                               .top = kButtonMargin,
                                               .bottom = kButtonMargin}});

  if (auto r = LoadRenderAssets(ctx); !r) {
    return r;
  }

  SetupAudio(ctx);
  BuildArena();
  // Stand up the weather producers ONCE, then wire the sim: systems register in
  // step order — weather FIRST so its wetness is fresh when movement reads it.
  const F32 weather_half_w =
      static_cast<F32>(CurrentMap()[0].size()) * 0.5f * kTile;
  const F32 weather_top =
      static_cast<F32>(CurrentMap().size()) * 0.5f * kTile + kTile;
  weather_.Setup(scene_, world_.World(), particle_tex_, kLayerWall,
                 weather_half_w, weather_top);
  world_.World().Bind(&scene_, kLayerWall, kLayerCoin);
  world_.AddSystem(weather_);  // 0: wind + rain/snow → wetness/accum (first)
  world_.AddSystem(
      movement_);  // 1: kinematics (reads the wetness weather wrote)
  world_.AddSystem(
      coins_system_);  // 2: after movement — reads the pos it just wrote
  world_.AddSystem(progress_system_);  // 3: after coins — tally + timer + win
  // Headless captures can't click "Play", so boot straight into gameplay (the
  // autopilot then drives the player); interactive runs open on the menu.
  if (ctx.Capturing()) {
    screens_.Push(std::make_unique<PlayScreen>(*this));
  } else {
    screens_.Push(std::make_unique<MenuScreen>(*this));
  }
  screens_.ApplyPending(ctx);
  LogInfo("Coin Rush: collect {} coins in {:.0f}s. WASD/drag, P pause.",
          world_.World().Progress().total, kTimeLimit);
  return {};
}

// Load the P0 post/lighting/bloom chain + the per-object sprite materials (all
// shader_studio shaders). GPU resources live in cr_view; this just delegates.
Result<void> CoinRushGame::LoadRenderAssets(const app::AppContext& ctx) {
  // shader_dir is this exe's compiled-shader location.
  // Empty means "whatever this build baked in", which is every desktop caller.
  const std::string shaders =
      shader_dir_.empty() ? std::string(AETHER_SHADER_DIR) : shader_dir_;
  if (auto r = renderer_.LoadPipeline(ctx, shaders.c_str()); !r) {
    return r;
  }
  return scene_materials_.Load(ctx, shaders.c_str());
}

// Frame-rate flow + presentation. Runs before the fixed step, so a screen
// swap takes effect for this frame's steps; keeps ticking while time is paused.
void CoinRushGame::Update(const app::AppContext& ctx, F32 dt) {
  // In UPDATE, so a swap lands on a frame boundary and never between two fixed
  // steps: a step is a pure function of (world, input, dt), and changing the
  // Before the fixed step reads the jump action: the normalized region must
  // resolve against this frame's framebuffer, and `simulate` runs after this.
  actions_.SetViewport(ctx.window.FramebufferSize());
  // Touch controls appear the moment there IS a touch device, and never before.
  // The player has already tapped the menu to reach a level, so the button is
  // there by the time it is needed.
  if (!touch_bound_ && !ctx.input.Touches().empty()) {
    actions_.BindRegion(kActionJump, kJumpButton);
    touch_bound_ = true;
  }
  pointer_on_jump_ =
      touch_bound_ && ctx.input.IsPointerDownInside(
                          JumpButtonPixels(ctx.window.FramebufferSize()));
  screens_.HandleInput(ctx);
  // Advance the fade BEFORE ApplyPending so a swap fired at full cover
  // applies this frame (hidden behind the cover).
  transition_.Update(dt);
  screens_.ApplyPending(ctx);
  screens_.Animate(ctx, dt);  // entry/exit fades — BuildUi stays a pure build
  // Runs on every screen, so a flash fired at round-end still fades over the
  // result screen.
  elapsed_ += dt;
  // P7 daylight factor 0→1 (0 = deep night, 1 = noon): drives the lighting
  // ambient + the renderer's runtime grade switch.
  day_ = 0.5f - 0.5f * std::cos(elapsed_ / kDayCycle * kTwoPi);
  day_ambient_ = Lerp(kAmbient, kDayAmbient, day_);
  score_punch_ = std::max(0.0f, score_punch_ - dt * 2.5f);  // ~0.4s
  flash_ = std::max(0.0f, flash_ - dt * 2.2f);              // ~0.45s
}

void CoinRushGame::FixedUpdate(const app::AppContext& ctx, F32 dt) {
  screens_.UpdateTop(ctx, dt);
}

// A read of post-step state into the frame snapshot — the material/glow writes
// below stay here because they need THIS frame's camera + player positions.
RenderFrame CoinRushGame::Extract(const app::AppContext& ctx) {
  // Per-object material animation lives in the view's SceneMaterials; the
  // per-coin spotlight (scene-node colour, from player pos) stays here.
  Vec2 cam_pos{0.0f, 0.0f};
  if (const scene::Node* c = scene_.Get(camera_)) {
    cam_pos = c->local.position.xy();
  }
  scene_materials_.Animate(ctx.renderer, elapsed_, cam_pos);
  UpdateCoinGlow();  // spotlight: coins near the player glow + bloom
  // Projection uses the LOGICAL (window) size so resolution_scale changes only
  // the render target's pixel count, not the visible world (the view upscales
  // the scaled scene to the backbuffer — no zoom).
  RenderFrame frame = scene_.BuildRenderFrame(
      Viewport{.width = ctx.window.FramebufferSize().width,
               .height = ctx.window.FramebufferSize().height});
  // Render the weather feature's particle fields (read-only) — the sim owns +
  // steps them; view just Emits.
  if (const auto* rain = weather_.Rain()) {
    rain->Emit(frame.items);  // velocity-stretched streaks over the scene
  }
  if (const auto* snow = weather_.Snow()) {
    snow->Emit(frame.items);  // falling flakes
  }
  weather_.Accum().Emit(frame.items, white_,
                        /*layer=*/110);  // snow piled on the ground
  // The renderer's deferred passes (a separate PostProcess hook, no frame) need
  // this frame's camera + lights + items — hand it the frame to stash.
  renderer_.Capture(frame);
  return frame;
}

RenderFrame CoinRushGame::BuildOverlay(const app::AppContext& ctx) {
  // The ABSTRACT pointer, not the mouse: on a phone there is no mouse, and the
  // menus are the first thing a finger lands on. Desktop is unaffected — with a
  // mouse present the pointer IS the mouse.
  ui_.BeginFrame(ctx.input.PointerPosition(), ctx.input.IsPointerDown(),
                 ctx.window.FramebufferSize(), font_.get(), white_,
                 ctx.reference_size);
  const F32 fog = weather_.Config().fog;
  if (fog > 0.0f) {  // white haze over the world (under the HUD)
    const Vec2 cv = ui_.Canvas();
    ui_.Panel(ui::Rect{.x = 0.0f, .y = 0.0f, .w = cv.x, .h = cv.y},
              Vec4{0.86f, 0.90f, 0.98f, fog * 0.5f});
  }
  screens_.BuildUi(ctx, ui_);
  if (flash_ > 0.0f) {  // P5: full-screen feedback flash over the HUD/screen
    const Vec2 cv = ui_.Canvas();
    ui_.Panel(
        ui::Rect{.x = 0.0f, .y = 0.0f, .w = cv.x, .h = cv.y},
        Vec4{flash_color_.x, flash_color_.y, flash_color_.z, flash_ * 0.5f});
  }
  transition_.Draw(ui_);  // full-screen fade, over everything
#ifdef AETHER_DEV_TOOLS
  if (ctx.HudVisible()) {  // scene-graph HUD, toggled together with F3
  }
#endif
  return ui_.EndFrame();
}

// The frame: scene offscreen, then the view's composite (deferred lighting +
// bloom + grade) to the backbuffer instead of the default UpscalePass.
app::RenderPipeline CoinRushGame::BuildPipeline() {
  app::RenderPipeline p;
  p.AddPass(app::ScenePass())
      .AddPass(StringId::FromRuntime("post"),
               [this](const app::FrameContext& c) {
                 PostProcess(c.app, c.target.color);
               });
  return p;
}

// The pipeline renders the world into `scene_color`; the view's Renderer
// threads it through deferred-lighting + bloom + grade to the backbuffer.
void CoinRushGame::PostProcess(const app::AppContext& ctx,
                               TextureHandle scene_color) {
  renderer_.Render(ctx, scene_color, day_ambient_, /*night=*/day_ < 0.45f);
}

// Fade, then swap the top screen (declared in coin_rush.hpp).
void CoinRushGame::TransitionTo(std::unique_ptr<GameScreen> next) {
  transition_.Start(kFadeTime, [this, n = std::move(next)]() mutable {
    screens_.Replace(std::move(n));
  });
}

// --- GameApi: the flow + state verbs the screens drive the game through -----
void CoinRushGame::StartLevel(int index) {
  current_level_ = std::clamp(index, 0, LevelCount() - 1);
  TransitionTo(std::make_unique<PlayScreen>(*this));
}

void CoinRushGame::NextLevel() {
  if (HasNextLevel()) {
    ++current_level_;
  }
  TransitionTo(std::make_unique<PlayScreen>(*this));  // else replay this level
}

void CoinRushGame::ToMenu() {
  TransitionTo(std::make_unique<MenuScreen>(*this));
}

PlayHud CoinRushGame::HudState() const {
  const RoundProgress& pr = world_.World().Progress();
  return PlayHud{.collected = pr.collected,
                 .total = pr.total,
                 .time_left = pr.time_left,
                 .elapsed = elapsed_,          // view state (glow-pulse timer)
                 .score_punch = score_punch_,  // view state (pickup juice)
                 .level_name = LevelName(current_level_)};
}

RoundResult CoinRushGame::RoundSummary() const {
  const RoundProgress& pr = world_.World().Progress();
  return RoundResult{
      .collected = pr.collected, .total = pr.total, .stars = pr.stars};
}

// Build the whole scene from the map (walls, coins, player, follow camera,
// emitters). Used at load and on every (re)start.
void CoinRushGame::BuildArena() {
  scene_ = scene::Scene{};  // fresh world — re-establish scene-level defaults
  scene_.SetDefaultCameraReference(reference_size_);
  world_.World().Progress().total = 0;  // counted as AddCoin builds them
  coins_.clear();
  const std::span<const std::string_view> map = CurrentMap();
  const auto rows = static_cast<int>(map.size());
  const auto cols = static_cast<int>(map[0].size());
  const F32 ox = (static_cast<F32>(cols) - 1.0f) / 2.0f;
  const F32 oy = (static_cast<F32>(rows) - 1.0f) / 2.0f;
  Vec3 spawn{0.0f, 0.0f, 0.0f};

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      const char cell = map[static_cast<Usize>(r)][static_cast<Usize>(c)];
      const Vec3 pos{(static_cast<F32>(c) - ox) * kTile,
                     (oy - static_cast<F32>(r)) * kTile, 0.0f};
      // Side-view: '#' is a solid ground/platform tile; '.' is open sky (the
      // parallax backdrop shows through) — no top-down floor carpet.
      if (cell == '#') {
        AddWall(pos);
      } else if (cell == 'o') {
        AddCoin(pos);
      } else if (cell == 'E') {
        AddExit(pos);
      } else if (cell == 'P') {
        spawn = pos;
      }
    }
  }

  // The player is an ANIMATED character (P2): sprite from the animation_studio
  // atlas, AnimatorComponent plays idle/walk × 4 dirs (StepWorld picks it).
  player_ = scene_.CreateNode(scene_.Root());
  world_.World().SetPlayer(player_);  // the sim's player handle (rebuilt/round)
  scene_.SetLocalTransform(
      player_, Transform{.position = spawn, .scale = Vec3{1.5f, 1.5f, 1.0f}});
  auto* ps = scene_.AddComponent<scene::SpriteComponent>(player_);
  ps->texture = player_anim_->Page();
  ps->blend = BlendClass::kTranslucent;
  ps->layer = 300;
  // P6: the player carries a warm, faintly flickering light (a torch).
  auto* plight = scene_.AddComponent<scene::LightComponent>(
      player_, kTorchRadius, Vec3{1.0f, 0.93f, 0.74f}, 2.4f);
  plight->SetFlicker(/*amount=*/0.02f,
                     /*speed=*/5.0f);  // visible torch wobble
  plight->SetCastsShadows(true);       // P6-C: the torch throws wall shadows
  auto* anim = scene_.AddComponent<scene::AnimatorComponent>(player_);
  for (Usize i = 0; i < player_anim_->Clips().size(); ++i) {
    const resources::AnimationClip& c = player_anim_->Clips()[i];
    anim->AddClip(static_cast<int>(i), c.frames, c.fps, c.loop);
  }
  anim->Play(ClipState("idle_down"));
  if (const auto* idle = player_anim_->Clip("idle_down"); idle) {
    ps->SetSpriteFrame(idle->frames[0]);  // show frame 0 before the first step
  }

  // vfx_studio effects → per-round burst-only emitters, each repositioned +
  // fired at its trigger (pickup / footstep / wall impact / win).
  coin_rt_ = BuildEffect(scene_, coin_fx_);
  walk_dust_rt_ = BuildEffect(scene_, walk_dust_fx_);
  wall_sparks_rt_ = BuildEffect(scene_, wall_sparks_fx_);
  goal_rt_ = BuildEffect(scene_, goal_fx_);

  // Camera at the origin (the arena centre), starting at the wide establishing
  // zoom for the menu backdrop; StartRound eases it in to the player.
  camera_ = scene_.CreateNode(scene_.Root());
  auto* cam = scene_.AddComponent<scene::CameraComponent>(camera_);
  cam->zoom = kMapZoom;  // inherits the app reference_size (720-tall design)
  // Modular camera effects, composed with follow via the additive offset
  // channel; tuned for the arena scale.
  auto* shake = scene_.AddComponent<scene::ShakeComponent>(camera_);
  shake->SetParams(/*max_offset=*/14.0f, /*decay=*/2.4f, /*frequency=*/28.0f);
  auto* push = scene_.AddComponent<scene::PushComponent>(camera_);
  push->SetParams(/*stiffness=*/200.0f, /*damping=*/19.0f, kPushMax);
  auto* follow = scene_.AddComponent<scene::FollowComponent>(camera_);
  follow->target = player_;
  follow->stiffness = 7.0f;
  scene_.SetActiveCamera(camera_);

  // Parallax starfield: a big quad PARENTED to the camera at the lowest layer;
  // its UVs tile the starfield and Extract drifts them with camera motion.
  bg_node_ = scene_.CreateNode(camera_);
  auto* bg = scene_.AddComponent<scene::SpriteComponent>(bg_node_);
  bg->texture = bg_tex_;
  bg->size = kBgSize;
  bg->uv = Rect{.x = 0.0f,
                .y = 0.0f,
                .width = kBgSize.x / kBgTileWorld,
                .height = kBgSize.y / kBgTileWorld};
  bg->layer = 0;  // behind everything
  bg->material = scene_materials_.Bg();
}

void CoinRushGame::AddTile(
    const resources::ResourceHandle<resources::Texture>& tex, Vec3 pos,
    U16 layer) {
  const scene::NodeId n = scene_.CreateNode(scene_.Root());
  scene_.SetLocalTransform(n, Transform{.position = pos});
  auto* s = scene_.AddComponent<scene::SpriteComponent>(n);
  s->texture = tex;
  s->size = Vec2{kTile, kTile};
  s->layer = layer;
  s->material = scene_materials_.Floor();  // P3: scrolling-UV shader (drift)
}

// A walkable water tile (P3): a 4-frame ocean sprite looped by an
// AnimatorComponent; above the floor, below walls/coins, no collider.
void CoinRushGame::AddWater(Vec3 pos) {
  const scene::NodeId n = scene_.CreateNode(scene_.Root());
  scene_.SetLocalTransform(n, Transform{.position = pos});
  auto* s = scene_.AddComponent<scene::SpriteComponent>(n);
  s->texture = ocean_tex_;
  s->size = Vec2{kTile, kTile};
  s->layer = 3;
  auto* anim = scene_.AddComponent<scene::AnimatorComponent>(n);
  anim->AddClip(0, resources::SliceGrid(/*cols=*/4, /*rows=*/1),
                /*fps=*/8.0f);
  anim->Play(0);
}

// The goal marker: a green gem that sits DIM (locked) until every coin is
// collected, then brightens; reaching it wins. Rebuilt each round.
void CoinRushGame::AddExit(Vec3 pos) {
  world_.World().Progress().exit_pos = pos.xy();
  const scene::NodeId n = scene_.CreateNode(scene_.Root());
  scene_.SetLocalTransform(
      n, Transform{.position = pos, .scale = Vec3{1.4f, 1.6f, 1.0f}});
  auto* s = scene_.AddComponent<scene::SpriteComponent>(n);
  s->texture = coin_tex_;
  s->color = Vec4{0.30f, 0.55f, 0.35f, 0.85f};  // locked: dim green
  s->blend = BlendClass::kTranslucent;
  s->layer = 260;
  exit_sprite_ = s;
}

void CoinRushGame::AddWall(Vec3 pos) {
  const scene::NodeId n = scene_.CreateNode(scene_.Root());
  scene_.SetLocalTransform(n, Transform{.position = pos});
  auto* s = scene_.AddComponent<scene::SpriteComponent>(n);
  s->texture = wall_tex_;
  s->size = Vec2{kTile, kTile};
  s->layer = 100;
  s->material = scene_materials_.Wall();  // wall normal (occluder for P6-C)
  auto* col = scene_.AddComponent<scene::ColliderComponent>(n);
  col->kind = scene::ShapeKind::kBox;
  col->half_extents = Vec2{kTile * 0.5f, kTile * 0.5f};
  col->layer = kLayerWall;
}

// A one-shot pickup pop: a ghost coin that scales up, fades, self-destroys.
// Decoupled from the real coin (removed at once) so it can't be re-collected.
void CoinRushGame::SpawnPickupPop(Vec3 pos) {
  const scene::NodeId n = scene_.CreateNode(scene_.Root());
  scene_.SetLocalTransform(n, Transform{.position = pos});
  auto* s = scene_.AddComponent<scene::SpriteComponent>(n);
  s->texture = coin_tex_;
  s->size = Vec2{kTile * 0.6f, kTile * 0.6f};
  s->blend = BlendClass::kTranslucent;
  s->layer = 250;  // above the coins
  scene_.RunAction(
      n,
      scene::Sequence(scene::Spawn(scene::ScaleTo(0.28f, 2.0f, ease::QuadOut),
                                   scene::FadeOut(0.28f, ease::QuadOut)),
                      scene::CallFunc([this, n] { scene_.DestroyNode(n); })));
}

void CoinRushGame::AddCoin(Vec3 pos) {
  const scene::NodeId n = scene_.CreateNode(scene_.Root());
  scene_.SetLocalTransform(n, Transform{.position = pos});
  auto* s = scene_.AddComponent<scene::SpriteComponent>(n);
  s->texture = coin_tex_;
  s->size = Vec2{kTile * 0.6f, kTile * 0.6f};
  s->blend = BlendClass::kTranslucent;
  s->layer = 200;
  s->material = scene_materials_.Coin();  // custom emissive-glow shader (P0)
  auto* col = scene_.AddComponent<scene::ColliderComponent>(n);
  col->kind = scene::ShapeKind::kCircle;
  col->radius = kTile * 0.3f;
  col->layer = kLayerCoin;
  col->trigger = true;
  // Coins do NOT cast fill light (it would wash out the torch's shadows); they
  // glow emissively + brighten by torch proximity (UpdateCoinGlow) instead.
  coins_.push_back(n);  // tracked for the spotlight proximity-glow
  ++world_.World().Progress().total;

  // Juice: a 2D "coin flip" (X-scale) + gentle float, phase-offset per coin so
  // the field shimmers rather than pulsing in lockstep (two RepeatForever).
  const F32 phase = std::fmod(std::abs(pos.x * 0.011f + pos.y * 0.017f), 1.0f);
  scene_.RunAction(
      n,
      scene::Sequence(
          scene::DelayTime(phase * 0.9f),
          scene::RepeatForever(scene::Sequence(
              scene::ScaleTo(0.55f, Vec3{0.16f, 1.0f, 1.0f}, ease::SineInOut),
              scene::ScaleTo(0.55f, Vec3{1.0f, 1.0f, 1.0f},
                             ease::SineInOut)))));
  scene_.RunAction(
      n, scene::Sequence(
             scene::DelayTime(phase * 0.8f),
             scene::RepeatForever(scene::Sequence(
                 scene::MoveBy(0.9f, Vec2{0.0f, 5.0f}, ease::SineInOut),
                 scene::MoveBy(0.9f, Vec2{0.0f, -5.0f}, ease::SineInOut)))));
}

// Spotlight reveal: ramp each coin's emissive up as the torch reaches it,
// dimming coins beyond kSpotRange. Driven from player pos each frame.
void CoinRushGame::UpdateCoinGlow() {
  const scene::Node* p = scene_.Get(player_);
  if (p == nullptr) {
    return;
  }
  const Vec2 player = p->local.position.xy();
  for (const scene::NodeId coin : coins_) {
    const scene::Node* node = scene_.Get(coin);
    if (node == nullptr) {
      continue;  // collected
    }
    auto* s = scene_.GetComponent<scene::SpriteComponent>(coin);
    if (s == nullptr) {
      continue;
    }
    const Vec2 d = node->local.position.xy() - player;
    const F32 dist = std::sqrt(d.x * d.x + d.y * d.y);
    F32 t = std::clamp(1.0f - dist / kSpotRange, 0.0f, 1.0f);
    t = t * t;  // ease so the ramp feels like a light edge
    const F32 b = kCoinDim + (kCoinBright - kCoinDim) * t;
    s->color = Vec4{b, b, b, 1.0f};
  }
}

void CoinRushGame::SetupAudio(const app::AppContext& ctx) {
  ctx.audio.SetMasterGain(0.8f);
  if (auto c = ctx.resources.Load<resources::AudioClip>(assets::audio::kCoin)) {
    coin_clip_ = *c;
  }
  if (auto bed =
          ctx.resources.Load<resources::AudioClip>(assets::audio::kAmbient)) {
    ctx.audio.Play(*bed, {.gain = 0.3f, .loop = true});
  }
  audio_ = &ctx.audio;
}

// The single place sim events become presentation/flow: every GameEvent the
// fixed step emits (Landed/WallHit/coin/exit/win) is drained through here.
void CoinRushGame::ApplyEvent(const GameEvent& e) {
  if (const auto* landed = std::get_if<Landed>(&e)) {
    walk_dust_rt_.SpawnAt(landed->point);  // landing puff
  } else if (const auto* wall = std::get_if<WallHit>(&e)) {
    if (wall_spark_timer_ <= 0.0f) {  // throttle distinct wall-spark bursts
      wall_spark_timer_ = kWallSparkGap;
      wall_sparks_rt_.SpawnAt(wall->point);
    }
  } else if (const auto* coin = std::get_if<CoinCollected>(&e)) {
    // The count + exit-unlock is the progression system's; view just reacts.
    score_punch_ = 1.0f;  // HUD score juice + "+1" popup
    if (audio_ != nullptr && coin_clip_) {
      audio_->Play(coin_clip_, {.gain = 0.9f});
    }
    coin_rt_.SpawnAt(coin->point);  // vfx_studio coin_pickup.vfx
    SpawnPickupPop(Vec3{coin->point.x, coin->point.y, 0.0f});
    if (auto* shake = scene_.GetComponent<scene::ShakeComponent>(camera_)) {
      shake->Shake(0.16f);  // a small knock per coin
    }
  } else if (std::get_if<ExitUnlocked>(&e) != nullptr) {
    if (exit_sprite_ != nullptr) {  // brighten the gem + a green flash
      exit_sprite_->color = Vec4{0.45f, 1.0f, 0.55f, 1.0f};
      TriggerFlash(Vec4{0.4f, 1.0f, 0.5f, 1.0f}, 0.4f);
    }
  } else if (std::get_if<RoundWon>(&e) != nullptr) {
    // Star rating from the soft timer (fast = 3, slow = 1).
    RoundProgress& pr = world_.World().Progress();
    pr.stars = 1;
    if (pr.time_left > kTimeLimit * 0.30f) {
      pr.stars = 2;
    }
    if (pr.time_left > kTimeLimit * 0.60f) {
      pr.stars = 3;
    }
    goal_rt_.SpawnAt(pr.exit_pos);  // vfx_studio goal_explosion.vfx
    TriggerFlash(Vec4{1.0f, 0.86f, 0.42f, 1.0f}, 0.6f);  // gold win flash
    if (auto* shake = scene_.GetComponent<scene::ShakeComponent>(camera_)) {
      shake->Shake(0.5f);
    }
    TransitionTo(std::make_unique<ResultScreen>(*this, /*won=*/true));
  }
}

// Reset counters + rebuild the arena for a fresh round.
void CoinRushGame::StartRound() {
  BuildArena();
  weather_.SetConfig(CurrentWeather());  // the level's rain/wind/snow/fog
  // Fresh (empty) ground snow surface for the level: a straight top-edge
  // polyline along the ground (world units; the app owns the tile metrics).
  const F32 cols = static_cast<F32>(CurrentMap()[0].size());
  const F32 rows = static_cast<F32>(CurrentMap().size());
  const F32 ground_top = -(rows - 1.0f) * 0.5f * kTile + kTile * 0.5f;
  weather_.RegisterSnowSurface((cols - 1.0f) * 0.5f * kTile, ground_top);
  RoundProgress& pr =
      world_.World().Progress();  // total set by BuildArena above
  pr.collected = 0;
  pr.time_left = kTimeLimit;
  pr.round_over = false;
  pr.stars = 0;
  pr.exit_unlocked = false;
  pr.win_radius = kPlayerRadius + kTile;
  dragging_ = false;
  score_punch_ = 0.0f;
  flash_ = 0.0f;
  movement_.Reset();
  // Establishing shot → play: ease the zoom in from the wide map-centre framing
  // while the follow pans onto the player.
  if (auto* cam = scene_.GetComponent<scene::CameraComponent>(camera_)) {
    cam->SetZoom(kMapZoom);
    cam->ZoomTo(kPlayZoom, kIntroZoom, ease::CubicOut);
  }
}

// Snap the camera to the wide, centred establishing shot for the menu backdrop
// (the scene isn't updated there, so it can't animate).
void CoinRushGame::FrameEstablishing() {
  if (auto* cam = scene_.GetComponent<scene::CameraComponent>(camera_)) {
    cam->SetZoom(kMapZoom);
  }
  scene_.SetLocalTransform(camera_, Transform{});  // map centre (origin)
}

// Pick the player's clip from movement (P2): walk_/idle_<facing>; facing is the
// dominant axis of `dir` and persists so idle faces the last-walked way.
void CoinRushGame::UpdatePlayerAnimation(Vec2 dir) {
  const bool moving = dir.x * dir.x + dir.y * dir.y > 0.02f;
  if (moving) {
    if (std::abs(dir.x) >= std::abs(dir.y)) {
      last_facing_ = dir.x < 0.0f ? "left" : "right";
    } else {
      last_facing_ = dir.y < 0.0f ? "down" : "up";  // world +Y is up
    }
  }
  const std::string clip = (moving ? "walk_" : "idle_") + last_facing_;
  if (auto* anim = scene_.GetComponent<scene::AnimatorComponent>(player_)) {
    anim->Play(ClipState(clip));
  }
}

// One kinematic step (accel/friction, coyote+buffered+variable jump, gravity,
// resolve vs tiles). Returns the resolve push so the caller knows what we hit.
Vec2 CoinRushGame::AdvancePlayer(const app::AppContext& /*ctx*/, F32 dt,
                                 const LatchedInput& in) {
  scene::Node* p = scene_.Get(player_);
  if (p == nullptr) {
    return Vec2{0.0f, 0.0f};
  }
  UpdatePlayerAnimation(Vec2{in.move, 0.0f});
  // The movement feature runs inside the deterministic fixed step and EMITS
  // events; the game drains them, keeping the controller free of side effects.
  const EventList& events = world_.Step(in, dt);
  // Drain every event from the step through the one reaction router.
  for (const GameEvent& e : events) {
    ApplyEvent(e);
  }
  const Vec2 blocked =
      movement_.LastBlocked();  // camera nudge + autopilot turn
  Vec2 push_target{0.0f, 0.0f};
  if (std::abs(blocked.x) > 0.5f) {
    push_target = Vec2{std::clamp(blocked.x, -kPushMax, kPushMax), 0.0f};
  }
  if (auto* push = scene_.GetComponent<scene::PushComponent>(camera_)) {
    push->SetTarget(push_target);  // consumed by scene_.Update below
  }
  scene_.Update(dt);
  return blocked;
}

// Persist the run's recorded inputs (if --record was requested) as the last act
// before members tear down — App::Run destroys us while services are still up.
CoinRushGame::~CoinRushGame() {
  if (record_path_.empty()) {
    return;
  }
  if (const auto r = SaveReplay(record_path_, recorder_.Steps())) {
    LogInfo("Coin Rush: recorded {} steps to {}", recorder_.Steps().size(),
            record_path_);
  } else {
    LogError("Coin Rush: failed to write replay {}: {}", record_path_,
             r.error().message);
  }
}

// Pick the frame's input source once from the run mode: replay file → capture
// autopilot → live keyboard, all feeding the SAME GameWorld.Step.
std::unique_ptr<InputSource> CoinRushGame::MakeInputSource(
    const app::AppContext& ctx) {
  if (!replay_path_.empty()) {
    if (auto steps = LoadReplay(replay_path_)) {
      LogInfo("Coin Rush: replaying {} steps from {}", steps->size(),
              replay_path_);
      return std::make_unique<ReplayInputSource>(std::move(*steps));
    }
    LogError("Coin Rush: cannot load replay {} — falling back to live input",
             replay_path_);
  }
  if (ctx.Capturing()) {
    return std::make_unique<AutopilotInputSource>(*this);
  }
  return std::make_unique<LiveInputSource>(*this);
}

// One fixed step of gameplay: the source produces the LatchedInput, we record
// it (so any run is replayable), step the world, then let the source observe.
void CoinRushGame::StepWorld(const app::AppContext& ctx, F32 dt) {
  if (world_.World().Progress().round_over) {
    return;
  }
  // Weather is the first system in world_.Step (registered in Load), so it runs
  // just below — no separate weather tick anymore.
  if (!input_) {
    input_ = MakeInputSource(ctx);
  }
  const LatchedInput in = input_->Next(ctx, dt);
  recorder_.Record(in);  // every run records its inputs → replay oracle
  const Vec2 blocked = AdvancePlayer(ctx, dt, in);
  input_->Observe(blocked, movement_.OnGround(), dt);

  // Motion trail (P2): while moving, drop a fading afterimage of the current
  // frame; spawned AFTER the scene update so it snapshots this frame's pose.
  const bool moving =
      movement_.OnGround() && std::abs(movement_.Velocity().x) > 24.0f;
  trail_timer_ -= dt;
  if (moving && trail_timer_ <= 0.0f) {
    trail_timer_ = 0.05f;
    SpawnTrailGhost();
  }

  // walk_dust.vfx: a scuff at the feet on a footstep cadence while moving.
  // wall_spark_timer_ ticks here so wall hits aren't gated by current contact.
  wall_spark_timer_ -= dt;
  dust_timer_ -= dt;
  if (moving && dust_timer_ <= 0.0f) {
    dust_timer_ = kDustInterval;
    if (const scene::Node* p = scene_.Get(player_)) {
      const Vec2 feet{p->local.position.x,
                      p->local.position.y - kPlayerRadius * 0.7f};
      walk_dust_rt_.SpawnAt(feet);
    }
  }

  // Timer, tally, exit-unlock and win are the progression system's; its
  // ExitUnlocked/RoundWon drive the gem flash / result screen.
}

// A translucent afterimage of the player's current frame, fading then
// self-destructing (the P2 motion trail); drawn just under the player.
void CoinRushGame::SpawnTrailGhost() {
  const scene::Node* p = scene_.Get(player_);
  const auto* ps = scene_.GetComponent<scene::SpriteComponent>(player_);
  if (p == nullptr || ps == nullptr) {
    return;
  }
  const scene::NodeId n = scene_.CreateNode(scene_.Root());
  scene_.SetLocalTransform(n, p->local);  // same pos + scale as the player
  auto* s = scene_.AddComponent<scene::SpriteComponent>(n);
  s->texture = ps->texture;
  s->uv = ps->uv;  // snapshot the current animation frame
  s->size = ps->size;
  s->blend = BlendClass::kTranslucent;
  s->color = Vec4{1.0f, 0.6f, 0.6f, 0.45f};  // faint red tint
  s->layer = 299;                            // just under the player (300)
  scene_.RunAction(
      n,
      scene::Sequence(scene::FadeOut(0.25f, ease::QuadOut),
                      scene::CallFunc([this, n] { scene_.DestroyNode(n); })));
}

// WASD combined with a hold-and-drag virtual stick (mouse/touch).
Vec2 CoinRushGame::ReadMove(const app::AppContext& ctx) {
  Vec2 dir{0.0f, 0.0f};
  const input::InputSnapshot& in = ctx.input;
  if (in.IsDown(platform::Key::kA) || in.IsDown(platform::Key::kLeft)) {
    dir.x -= 1.0f;
  }
  if (in.IsDown(platform::Key::kD) || in.IsDown(platform::Key::kRight)) {
    dir.x += 1.0f;
  }
  if (in.IsDown(platform::Key::kW) || in.IsDown(platform::Key::kUp)) {
    dir.y += 1.0f;
  }
  if (in.IsDown(platform::Key::kS) || in.IsDown(platform::Key::kDown)) {
    dir.y -= 1.0f;
  }
  dir += DragMove(ctx);
  return dir;
}

bool CoinRushGame::TakeJumpPress(const app::AppContext& ctx) {
  return actions_.TakePressed(ctx.sim_input, kActionJump);
}

bool CoinRushGame::JumpHeld(const app::AppContext& ctx) const {
  return actions_.IsDown(ctx.input, kActionJump);
}

Vec2 CoinRushGame::DragMove(const app::AppContext& ctx) {
  // The abstract pointer, so a finger drives the stick as the mouse does —
  // which is what the "(mouse/touch)" in ReadMove's comment always promised.
  const bool pressed = ctx.input.IsPointerDown();
  drag_pos_ = ctx.input.PointerPosition();
  if (!pressed) {
    dragging_ = false;
    return Vec2{0.0f, 0.0f};
  }
  if (!dragging_) {
    if (ui_.WantsPointer()) {
      return Vec2{0.0f, 0.0f};  // press began on UI → not a move
    }
    // A thumb on the jump button is not a steer. The button is drawn as art
    // rather than a ui widget, so WantsPointer does not cover it, and this is
    // the check that keeps tapping jump from also yanking the stick. Tested as
    // the REGION, not the jump action: the action includes Space, and holding
    // Space must not stop the mouse dragging. Latched in Update, before steps.
    if (pointer_on_jump_) {
      return Vec2{0.0f, 0.0f};
    }
    dragging_ = true;
    drag_anchor_ = drag_pos_;
  }
  const F32 dx = drag_pos_.x - drag_anchor_.x;
  const F32 dy = drag_pos_.y - drag_anchor_.y;
  const F32 len = std::sqrt(dx * dx + dy * dy);
  if (len <= kDragDeadzone) {
    return Vec2{0.0f, 0.0f};
  }
  const F32 mag = std::min(len / kDragRadius, 1.0f);
  return Vec2{dx / len * mag, -dy / len * mag};  // screen +Y down → world up
}

TextureHandle CoinRushGame::MakeSoftDot(const app::AppContext& ctx) {
  constexpr U32 kDot = 16;
  std::array<U8, static_cast<Usize>(kDot) * kDot * 4> dot{};
  for (U32 y = 0; y < kDot; ++y) {
    for (U32 x = 0; x < kDot; ++x) {
      const F32 dx = (static_cast<F32>(x) + 0.5f) / kDot * 2.0f - 1.0f;
      const F32 dy = (static_cast<F32>(y) + 0.5f) / kDot * 2.0f - 1.0f;
      const F32 fall = 1.0f - std::sqrt(dx * dx + dy * dy);
      const auto a = static_cast<U8>(Saturate(fall * fall) * 255.0f);
      const Usize i = (static_cast<Usize>(y) * kDot + x) * 4;
      dot[i] = 255;
      dot[i + 1] = 255;
      dot[i + 2] = 255;
      dot[i + 3] = a;
    }
  }
  if (auto t =
          ctx.device.CreateTexture(kDot, kDot, rhi::TextureFormat::kRGBA8,
                                   dot.data(), static_cast<U32>(dot.size()))) {
    return *t;
  }
  return {};
}

// HUD helpers shared by the play + result screens.
void CoinRushGame::DrawJoystick(ui::Context& ui) {
  if (!dragging_) {
    return;
  }
  // The stick tracks the mouse (framebuffer pixels); the UI now works in
  // design units, so convert positions + radii by 1/UiScale.
  const F32 inv = ui.UiScale() > 0.0f ? 1.0f / ui.UiScale() : 1.0f;
  const F32 ax = drag_anchor_.x * inv;
  const F32 ay = drag_anchor_.y * inv;
  const F32 br = kDragRadius * inv;
  ui.Image(ui::Rect{.x = ax - br, .y = ay - br, .w = 2 * br, .h = 2 * br},
           particle_tex_, kFullUv, Hex(0xc7dcd0, 0.18f));
  F32 dx = (drag_pos_.x - drag_anchor_.x) * inv;
  F32 dy = (drag_pos_.y - drag_anchor_.y) * inv;
  if (const F32 l = std::sqrt(dx * dx + dy * dy); l > br) {
    dx = dx / l * br;
    dy = dy / l * br;
  }
  const F32 kr = 34.0f * inv;
  ui.Image(
      ui::Rect{.x = ax + dx - kr, .y = ay + dy - kr, .w = 2 * kr, .h = 2 * kr},
      particle_tex_, kFullUv, Hex(0xf9c22b, 0.8f));
}

// The touch jump button. Art only — the action map owns the hitbox, from the
// same kJumpButton rect, so what is drawn is exactly what is pressable.
void CoinRushGame::DrawJumpButton(ui::Context& ui) {
  if (!touch_bound_) {
    return;  // no touch device: the keyboard is the control, this is clutter
  }
  const F32 inv = ui.UiScale() > 0.0f ? 1.0f / ui.UiScale() : 1.0f;
  const Vec2 canvas = ui.Canvas();
  // Design units = normalized * canvas, since the canvas IS the framebuffer
  // scaled by UiScale.
  const F32 bx = kJumpButton.x * canvas.x;
  const F32 by = kJumpButton.y * canvas.y;
  const F32 bw = kJumpButton.width * canvas.x;
  const F32 bh = kJumpButton.height * canvas.y;
  // Brightens while held, which is the only feedback a finger gets — it covers
  // the button, so there is no hover state to lean on.
  const bool held = pointer_on_jump_;
  const F32 radius = 0.5f * std::min(bw, bh);
  const F32 cx = bx + bw * 0.5f;
  const F32 cy = by + bh * 0.5f;
  ui.Image(ui::Rect{.x = cx - radius,
                    .y = cy - radius,
                    .w = 2.0f * radius,
                    .h = 2.0f * radius},
           particle_tex_, kFullUv,
           held ? Hex(0xf9c22b, 0.85f) : Hex(0xc7dcd0, 0.30f));
  ui.LabelCentered(cx, cy - 10.0f * inv, "JUMP",
                   held ? Hex(0x2b2233, 1.0f) : kUiTextDim);
}

// Draw a window/panel background as the ui_studio 9-slice texture (falls back
// to a flat theme panel if the texture is missing).
void CoinRushGame::DrawPanel(ui::Context& ui, const ui::Rect& rect) {
  if (!panel_tex_) {
    ui.Panel(rect, kUiPanel);
    return;
  }
  ui.NineSlice(rect, panel_tex_->Handle(), panel_tex_->Width(),
               panel_tex_->Height(),
               ui::NineSliceMargins{.left = kPanelMargin,
                                    .right = kPanelMargin,
                                    .top = kPanelMargin,
                                    .bottom = kPanelMargin});
}

}  // namespace game
