#include "hearthfield.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>

#include "aether/core/log.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/platform/clock.hpp"
#include "aether/platform/file_system.hpp"
#include "aether/platform/key.hpp"
#include "aether/platform/user_data.hpp"
#include "aether/platform/window.hpp"
#include "aether/resources/font.hpp"
#include "aether/resources/resource_manager.hpp"
#include "aether/resources/texture.hpp"
#include "aether/resources/world_chunk.hpp"
#include "aether/scene/world_instance.hpp"
#include "aether/ui/layout.hpp"
#include "aether/ui/layout_lint.hpp"
#include "hf/content/animals.hpp"
#include "hf/content/crops.hpp"
#include "hf/content/farm.hpp"
#include "hf/content/islands.hpp"
#include "hf/content/items.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/save.hpp"
#include "hf/runtime/script.hpp"
#include "hf/view/loading.hpp"
#include "hf/view/palette.hpp"

namespace hearthfield::app {
namespace {

using namespace aether;

// The scripted session --autopilot plays: turn, tap a few plots, turn back. It
// exists so `--frames N` means the same thing on every machine — the same
// reason lantern's --demo does.
constexpr U32 kAutopilotPeriod = 20;

constexpr std::string_view kAppName = "hearthfield";
constexpr std::string_view kSaveFile = "farm.hfsv";
// Often enough that a force-quit — which is how a phone game is closed — loses
// at most a minute, rare enough that the disk is not the loop's cost. Paired
// with the digest test, so an idle farm writes nothing at all.
constexpr F32 kAutosaveSeconds = 60.0f;

// HOW LONG A CROSSING TAKES, and it is a budget as much as a pace: GEA
// §16.4.2's air lock only works while the transit outlasts the load it hides.
// s6 measures the load; if it ever exceeds this, the answer is async loading,
// not a longer flight — a crossing stretched to cover a stall is a loading
// screen with scenery.
constexpr F32 kTransitSeconds = 2.0f;

// THE ARCHIPELAGO AS SEEN FROM WHEREVER YOU ARE: one cheap mesh per island the
// player is NOT on, standing at its own world position.
//
// Built in code rather than authored as a `.world.json`, and that is the whole
// point — the positions live once, in content::kIslands. A proxy chunk on disk
// would be a second copy of every island's coordinates, which is exactly the
// duplication s3 spent its effort removing from camera.hpp.
// `also_loaded` is the OTHER end of a crossing: during a flight both islands
// are real geometry, so neither may also stand here as a silhouette. Pass
// `resident` twice when standing still.
[[nodiscard]] resources::WorldChunk NeighbourSilhouettes(
    content::IslandId resident, content::IslandId also_loaded) {
  std::vector<resources::WorldEntity> entities;
  for (content::IslandId id = 0; id < content::kIslands.size(); ++id) {
    if (id == resident || id == also_loaded) {
      continue;  // the real thing is loaded; a proxy would double it
    }
    const content::Island& island = content::kIslands[id];
    std::vector<resources::Attribute> attrs;
    attrs.push_back({.key = "model", .value = std::string(island.proxy_mesh)});
    attrs.push_back(
        {.key = "pos",
         .value = std::vector<F64>{island.world_pos.x, island.world_pos.y,
                                   island.world_pos.z}});
    // CASTS NOTHING AND RECEIVES NOTHING. A silhouette 90 m away that throws a
    // shadow throws it onto the island you ARE on, which is a hard edge from an
    // object the player cannot see the point of — and every caster triangle is
    // counted twice by the cost oracle (event:2026-08-25#102).
    attrs.push_back({.key = "casts_shadows", .value = false});
    attrs.push_back({.key = "receives_shadows", .value = false});
    entities.push_back({.type = "static_mesh",
                        .attrs = resources::Attributes(std::move(attrs))});
  }
  return resources::WorldChunk("neighbours", std::move(entities));
}

}  // namespace

Result<void> HearthfieldGame::RunUiLint(const aether::app::AppContext& ctx) {
  // THE SIZES ARE THE ONES THAT BROKE, not a sweep. 1280x720 is what every
  // capture uses and where nothing was ever wrong; 800x450 is where the HUD
  // chips first collided with the tab strip; 720x1280 is Android's native
  // orientation, where the coop warning was dropped. The two extremes bracket
  // the range rather than sampling it.
  static constexpr std::array<Size, 5> kSizes{
      Size{.width = 1280, .height = 720}, Size{.width = 1920, .height = 1080},
      Size{.width = 800, .height = 450}, Size{.width = 720, .height = 1280},
      Size{.width = 640, .height = 360}};
  // Index 0 is the bare HUD — the tab-strip collision happens with no screen
  // open at all — so a variant maps to `which` as index - 1.
  static constexpr std::array<const char*, 4> kNames{"(hud only)", "barn",
                                                     "shop", "orders"};

  // The sweep itself lives in `ui` (SweepLayout); this callback is the part
  // only Hearthfield can write.
  return ui::SweepLayout(ui_, kSizes, kNames, [&](Size fb, Usize index) {
    const int which = static_cast<int>(index) - 1;
    while (!screens_.Empty()) {
      screens_.Pop();
      ScreenCtx drain{.api = runtime::GameApi(latched_),
                      .snapshot = &world_.Snapshot()};
      screens_.ApplyPending(drain);
    }
    if (which == 0) {
      screens_.Push(std::make_unique<BarnScreen>());
    } else if (which == 1) {
      screens_.Push(std::make_unique<ShopScreen>());
    } else if (which == 2) {
      screens_.Push(std::make_unique<OrdersScreen>());
    }

    // APPLY THE PUSH FIRST. BuildOverlay draws the tabs BEFORE ApplyPending, so
    // on the single frame a screen is opened the stack is still empty and the
    // tabs are emitted; from the next frame on they are not. Linting that
    // transient measures a state the player never sees for longer than 16 ms.
    {
      ScreenCtx pending{.api = runtime::GameApi(latched_),
                        .snapshot = &world_.Snapshot()};
      screens_.ApplyPending(pending);
    }

    // Build the frame exactly as BuildOverlay does. Deliberately the same calls
    // rather than a simplified stand-in: a lint that exercises its own
    // arrangement of the UI checks an arrangement the game never draws.
    const runtime::ViewSnapshot& snapshot = world_.Snapshot();
    ui_.BeginFrame(Vec2{-1.0f, -1.0f}, false, fb, font_.get(),
                   ctx.resources.WhiteTexture()
                       ? ctx.resources.WhiteTexture()->Handle()
                       : TextureHandle{},
                   ctx.reference_size);
    view::DrawHud(ui_, snapshot, panel_tex_.get());
    BuildTabs();
    ScreenCtx screen_ctx{.api = runtime::GameApi(latched_),
                         .snapshot = &snapshot,
                         .panel = panel_tex_.get()};
    screens_.ApplyPending(screen_ctx);
    screens_.BuildUi(screen_ctx, ui_);
    // The frame is built for its LAYOUT, not to be drawn — the audit is the
    // output and the RenderFrame is discarded.
    static_cast<void>(ui_.EndFrame());
  });
}

void HearthfieldGame::BuildTabs() {
  // The three buttons that OPEN the in-game GUI. In app/ rather than in the
  // HUD because opening a screen is FLOW, and flow is app/'s (view/ may not
  // even name a ScreenStack).
  // CENTRED IN THE BAR, not pinned near its top: the bar is
  // `kTouchTarget + kHudPad` tall and the tabs are `kTouchTarget`, so half the
  // padding above and half below is the only placement that looks deliberate.
  // They sat at a quarter of the padding, which left three units above and nine
  // below and read as slightly fallen off the top.
  // THE SAME LAYOUT THE HUD DREW ITSELF FROM, not a second derivation. This
  // recomputed the strip's origin from `Canvas().x` while view/ computed the
  // space left for it from `kTabStrip`; the two disagreed, and chips vanishing
  // under the buttons is what that disagreement looked like. The HUD is now a
  // CARD inset from the window, so a canvas-relative position would also put
  // these outside it.
  const view::HudLayout hud = view::LayoutHud(ui_.Canvas());
  // The strip flows along the row `LayoutHud` reserved for it, so app/ can no
  // longer advance past what view/ kept clear — the two disagreed once already.
  ui::Stack strip{hud.tabs, ui::Axis::kHorizontal, view::kTabGap};
  // A SCREEN SWALLOWS THESE, and in an immediate-mode UI that has to be said
  // out loud. The scrim is drawn LATER and therefore sorts above the tabs —
  // measured, (79,82,124) dimming to (44,45,68), exactly its 0.45 alpha — but
  // `ui_.Button` hit-tests the moment it is called, which is before the screen
  // stack has drawn anything at all. So the tabs looked covered and stayed
  // clickable: pressing Shop under an open Barn stacked a second screen.
  //
  // `BlockPointer` does not help here. That claims the pointer against the
  // GAME, which is what stopped the board panning underneath; it says nothing
  // about one widget declared before another.
  //
  // The result is dropped rather than the tabs being skipped, so the dimmed
  // strip stays visible through the scrim — a HUD you can still read is the
  // point of a translucent one. They do still take hover styling under the
  // panel, which is cosmetic and the only part of this left wrong.
  // DRAWN BUT INERT while a screen is open, so the dimmed strip stays readable
  // through the scrim without taking a click. Dropping the RESULT was not
  // enough and the lint proved it: at 800x450 the Shop tab (562..662) overlaps
  // an open panel's close button (620..664), the tab is declared FIRST, and it
  // took the press and discarded it — the X silently stopped closing the
  // screen. `PushInert` is what makes "covered" and "disabled" the same thing.
  const bool covered = !screens_.Empty();
  if (covered) {
    ui_.PushInert();
  }
  const auto tab = [&](std::string_view label) {
    return ui_.Button(strip.Take(view::kTabWidth), label);
  };
  if (tab("Barn")) {
    screens_.Push(std::make_unique<BarnScreen>());
  }
  if (tab("Shop")) {
    screens_.Push(std::make_unique<ShopScreen>());
  }
  if (tab("Orders")) {
    screens_.Push(std::make_unique<OrdersScreen>());
  }
  if (covered) {
    ui_.PopInert();
  }
}

aether::app::RenderPipeline HearthfieldGame::BuildPipeline() {
  // HdrPipeline with the background rain spliced BETWEEN its two passes, which
  // is the only place it can go. Before the scene there is no depth to test
  // against; after the tonemap the target is display-referred and resolved, so
  // the streaks would neither be occluded by an island nor tonemapped with the
  // rest of the frame.
  aether::app::RenderPipeline pipeline;
  pipeline.AddPass(aether::app::HdrScenePass(/*clear=*/true, &msaa_));
  pipeline.AddPass(aether::StringId::FromRuntime("rain_pass"),
                   [this](const aether::app::FrameContext& c) {
                     weather_.SubmitBackground(c.app.device, c.frame.view,
                                               c.target.fb);
                   });
  pipeline.AddPass(aether::app::TonemapPass(&tonemap_));
  return pipeline;
}

// The interface's own assets, all optional and all cheap. Loaded in the
// prologue rather than the tail because the loading screen between the phases
// draws with them (see Load).
void HearthfieldGame::LoadUiAssets(const aether::app::AppContext& ctx) {
  // THE GAME OWNS ITS PALETTE. Without this the widgets shade themselves with
  // ui::Theme's engine defaults — a cool purple-blue — which is the one thing
  // on screen that belongs to a different game (see hf/view/palette.hpp).
  ui_.SetTheme(view::Theme());

  // THE 9-SLICE SURFACES, all optional. A missing one costs the rounded frame
  // and nothing else — the same bargain the font makes two lines down, and the
  // reason `Frame()` still knows how to draw a flat panel.
  const auto load_tex = [&](std::string_view path,
                            std::shared_ptr<const resources::Texture>& out) {
    if (auto tex = ctx.resources.Load<resources::Texture>(std::string(path))) {
      out = *tex;
    } else {
      LogWarn("hearthfield: no {} — the UI keeps its flat surfaces", path);
    }
  };
  load_tex("textures/panel.png", panel_tex_);
  load_tex("textures/btn_normal.png", btn_normal_);
  load_tex("textures/btn_hover.png", btn_hover_);
  load_tex("textures/btn_pressed.png", btn_pressed_);
  if (btn_normal_ && btn_hover_ && btn_pressed_) {
    // ALL THREE OR NONE: a skin with one state missing draws an invalid handle,
    // which samples nothing and renders the button INVISIBLE while it still
    // works (event:2026-08-24#10, the same trap as the missing white texture).
    ui_.SetButtonSkin(
        ui::ButtonSkin{.normal = btn_normal_->Handle(),
                       .hover = btn_hover_->Handle(),
                       .pressed = btn_pressed_->Handle(),
                       .tex_width = btn_normal_->Width(),
                       .tex_height = btn_normal_->Height(),
                       .margins = {.left = view::kNineSliceMargin,
                                   .right = view::kNineSliceMargin,
                                   .top = view::kNineSliceMargin,
                                   .bottom = view::kNineSliceMargin}});
  }

  // Optional, like lantern's: a missing font should cost the text, not the
  // game. The engine already degrades a bad texture to a visible placeholder.
  if (auto font = ctx.resources.Load<resources::Font>("fonts/hud.fnt")) {
    font_ = *font;
  } else {
    LogWarn("hearthfield: no HUD font — the interface will draw unlabelled");
  }
}

Result<void> HearthfieldGame::Load(const aether::app::AppContext& ctx) {
  // kShared, and it is load-bearing rather than tidy: the instanceable
  // predicate requires an equal MaterialHandle id, so under kUnique every plot
  // would carry its own handle and the board would be one draw per tile.
  materials_ = std::make_unique<aether::app::PbrMaterialFactory>(
      ctx.renderer, aether::app::MaterialSharing::kShared);
  spawners_ =
      std::make_unique<aether::app::EngineSpawners>(ctx.resources, *materials_);

  // THE LOADING SCREEN'S OWN ASSETS COME FIRST, and this is the one ordering
  // the slicing introduced rather than inherited: every phase below is
  // presented on top of a UI that must already have a theme, a skin and a
  // font. It costs 5 ms of a 350 ms load, so it is also the cheapest possible
  // prologue.
  LoadUiAssets(ctx);

  // BEFORE THE CHUNKS, and it moved here at s4 for one reason: the save now
  // records WHICH ISLAND the player was standing on, and that decides what to
  // load. It was below the chunks while the answer was always the hub. Nothing
  // between here and there touches `world_`, and its old constraint still
  // holds — it runs before the board is built, because a loaded farm decides
  // how many plots there are, and it replaces `world_`, so AddSystem must come
  // after it.
  LoadFarm(ctx);

  // THE ISLAND DECIDES WHAT LOADS, not this file: content::kIslands names the
  // island's own chunk and the zone standing on it, and app/ walks that list
  // rather than knowing any path.
  //
  // --island OVERRIDES THE SAVE and unlocks nothing. Travel is s5, so this is
  // the only way to enter a satellite and therefore the only way this
  // milestone's "playable" is a claim anyone can check. A bad id is refused
  // rather than clamped: silently landing on the hub would read as the flag
  // being ignored.
  content::IslandId island_id = world_.World().Isles().current;
  if (island_override_ >= 0) {
    island_id = static_cast<content::IslandId>(island_override_);
    if (!content::IslandExists(island_id)) {
      return Fail(Errc::kInvalidArgument,
                  "--island " + std::to_string(island_override_) +
                      " is not an island; this build has " +
                      std::to_string(content::kIslands.size()));
    }
  }
  const content::Island* island = content::FindIsland(island_id);
  if (island == nullptr) {
    return Fail(Errc::kInvalidArgument,
                "content::kIslands has no hub — the game has nowhere to start");
  }
  LogInfo("hearthfield: on island {} ({}), {} of {} unlocked", island_id,
          island->name, std::popcount(world_.World().Isles().unlocked),
          content::kIslands.size());
  island_ = island_id;
  return {};
}

// ONE PHASE PER CALL, even where two are cheap: a phase sharing a frame with
// another is a phase whose cost cannot be read off the loading screen. A
// measuring run never gets here frame by frame — the loop drains this before
// frame 0, so no capture index moves.
Result<aether::app::LoadProgress> HearthfieldGame::LoadStep(
    const aether::app::AppContext& ctx) {
  if (load_phase_ == LoadPhase::kDone) {
    return aether::app::LoadProgress{};
  }
  const auto run = [&]() -> Result<void> {
    switch (load_phase_) {
      case LoadPhase::kPersistent:
        return LoadPersistent(ctx);
      case LoadPhase::kIsland:
        return LoadIsland(ctx);
      case LoadPhase::kNeighbours:
        return RespawnNeighbours(island_, island_);
      case LoadPhase::kFarmView:
        return LoadFarmView(ctx);
      case LoadPhase::kWeather:
        // AFTER the board and the steading: the rain's ground collider wants
        // the board's size, and the query snapshots world proxies.
        return weather_.Create(ctx.device, scene_, grid_);
      case LoadPhase::kSystems:
        return LoadSystems(ctx);
      case LoadPhase::kDone:
        break;
    }
    return {};
  };
  if (auto ok = run(); !ok) {
    return std::unexpected(ok.error());
  }
  load_phase_ = static_cast<LoadPhase>(static_cast<Usize>(load_phase_) + 1);
  return LoadingProgress();
}

// ONE source for the fraction and the label, because BuildOverlay draws them
// on the frame LoadStep's return value drove — two copies of this arithmetic
// would be a bar that disagrees with the loop about how far along it is.
aether::app::LoadProgress HearthfieldGame::LoadingProgress() const {
  // What is being loaded NEXT, which is what the frame presented after the
  // step just taken will be showing. Indexed by LoadPhase.
  static constexpr std::array<std::string_view,
                              static_cast<Usize>(LoadPhase::kDone) + 1>
      kLabels = {"the sky",  "the island",  "the archipelago",
                 "the farm", "the weather", "the simulation",
                 "ready"};
  const auto reached = static_cast<Usize>(load_phase_);
  return aether::app::LoadProgress{
      .done = load_phase_ == LoadPhase::kDone,
      .fraction = static_cast<F32>(reached) /
                  static_cast<F32>(static_cast<Usize>(LoadPhase::kDone)),
      .label = kLabels[reached]};
}

Result<LoadedChunk> HearthfieldGame::LoadChunk(
    const aether::app::AppContext& ctx, std::string_view path,
    scene::NodeId parent) {
  auto text = ctx.assets.ReadText(std::string(path));
  if (!text) {
    return std::unexpected(text.error());
  }
  auto chunk = resources::ParseWorldChunk(*text, std::string(path));
  if (!chunk) {
    return std::unexpected(chunk.error());
  }
  auto instance = scene::InstantiateChunk(scene_, parent, *chunk, *spawners_);
  if (!instance) {
    return std::unexpected(instance.error());
  }
  return LoadedChunk{std::move(*chunk), std::move(*instance)};
}

Result<void> HearthfieldGame::LoadPersistent(
    const aether::app::AppContext& ctx) {
  const content::Island* island = content::FindIsland(island_);
  if (island == nullptr) {
    return Fail(Errc::kInvalidArgument, "the resident island vanished");
  }
  // FIRST AND NEVER UNLOADED (GEA §16.4.2's load-and-stay-resident): the
  // camera, the sun and the sky. s5 frees the island being left, and it must
  // not take the player's viewpoint with it.
  auto persistent = LoadChunk(ctx, content::kPersistentPath, scene_.Root());
  if (!persistent) {
    return std::unexpected(persistent.error());
  }

  // ASK THE SCENE which node is the camera rather than indexing the file:
  // adding an entity must not silently renumber it (lantern's rule, and the
  // same reason).
  for (const scene::NodeId id : persistent->instance.nodes) {
    if (auto* projection = scene_.GetComponent<scene::CameraComponent>(id)) {
      camera_ = id;
      projection_ = projection;
      orbit_ = scene_.GetComponent<scene::OrbitComponent>(id);
      break;
    }
  }
  if (projection_ == nullptr || orbit_ == nullptr) {
    return Fail(Errc::kInvalidArgument,
                "the persistent chunk has no orbiting camera — the board needs "
                "one, and since s3 it is the world's camera rather than the "
                "farm's");
  }
  // CHECKED, not assumed. The whole board is designed around a parallel
  // projection, and a world file saying "perspective" would still load, still
  // render and merely look wrong — the failure mode ADR-0081 exists to end.
  if (projection_->projection != ProjectionMode::kOrthographic3D) {
    return Fail(Errc::kInvalidArgument,
                "the farm camera must be orthographic3d — the board is sized "
                "in world units and a perspective camera foreshortens it");
  }
  // The rig ADOPTS the authored framing rather than imposing one: the yaw the
  // art placed becomes detent zero, and the authored half-height the starting
  // zoom. The authored PITCH goes in too: the rig never changes it, but it
  // decides how much ground a screen height covers, and therefore how close to
  // the field's edge the focus may get before the void comes into frame.
  // Bounds come from the ISLAND, not the grid: the farm is one zone of it and
  // the rest is worth panning to. They carry no field extent, so the view is
  // free to run off the rim into sky — which is the whole point of the edge.
  //
  // The half-height is where ONE persistent camera pays its way: an island may
  // override the authored framing, because a satellite at a different scale
  // cannot be framed like the hub. The hub does not, so this is the number the
  // art placed.
  const F32 half_height =
      island->ortho_half_height.value_or(projection_->ortho_half_height);
  rig_.Configure(camera_tuning_, island->Roam(), orbit_->yaw,
                 view::FitHalfHeight(half_height, ctx.render_size),
                 orbit_->pitch);
  camera_input_.emplace(camera_tuning_);
  return {};
}

// The island itself, and whatever is BUILT on it. Both chunks in one phase
// because they hang off one node and a half-spawned island is not a state
// anything downstream is written for.
Result<void> HearthfieldGame::LoadIsland(const aether::app::AppContext& ctx) {
  const content::Island* island = content::FindIsland(island_);
  if (island == nullptr) {
    return Fail(Errc::kInvalidArgument, "the resident island vanished");
  }
  // AN ISLAND STANDS WHERE IT FLOATS, and ONE NODE OWNS ALL OF IT. The chunks'
  // entities are authored in island-local metres and this node carries the
  // world offset, so a satellite's future steading can be authored around 0,0
  // exactly as the farm's was. A no-op for the hub, which IS the origin — and
  // that is why board_/buildings_/ring_ can keep working in origin-relative
  // coordinates. One node also makes the unload a single DestroyNode, which is
  // why scene::WorldInstance carries a wrapper root at all (GEA §16.4.1).
  island_root_ = scene_.CreateNode(scene_.Root());
  if (!island_root_.Valid()) {
    return Fail(Errc::kInvalidArgument, "the scene refused an island node");
  }
  scene_.SetLocalTransform(island_root_,
                           Transform{.position = island->world_pos});

  // The island itself. Its nodes are deliberately NOT searched for named
  // entities below: it is ground and tree cover, it names nothing the game
  // looks up, and scenery that could rename the camera would be a trap.
  auto ground = LoadChunk(ctx, island->chunk, island_root_);
  if (!ground) {
    return std::unexpected(ground.error());
  }

  // The zone — what is BUILT on this island. An island may have none, which is
  // how s4 says "bare rock" without a branch here; a DECLARED zone that will
  // not load is a content bug and fails loudly, unlike the apron it replaces,
  // whose absence merely made the field look bare.
  load_zone_.reset();
  if (!island->zone.empty()) {
    auto loaded = LoadChunk(ctx, island->zone, island_root_);
    if (!loaded) {
      return std::unexpected(loaded.error());
    }
    load_zone_ = std::move(*loaded);
  }
  return {};
}

// THE FARM IS DRAWN ONLY WHERE THE FARM IS. The board, the buildings and the
// steading are the hub's ZONE made visible, and the sim owning them does not
// change which island they stand on — without this guard a satellite gets 576
// plots and a windmill floating on bare rock. The SIM still runs everywhere,
// which is right: crops grow while the player is away from them, exactly as
// they grow while the game is shut.
Result<void> HearthfieldGame::LoadFarmView(const aether::app::AppContext& ctx) {
  audio_.Load(ctx.resources);
  audio_system_ = &ctx.audio;
  if (load_zone_) {
    // ONE call, shared with arrival — see BuildFarmView for why the board, the
    // buildings and the steading are built in one place rather than three.
    if (auto built = BuildFarmView(ctx, *load_zone_); !built) {
      return built;
    }
  } else {
    // Nothing to build on and nothing to build with. Cleared rather than
    // guarded at each use, so the mode simply cannot be entered here.
    build_kind_ = runtime::LatchedInput::kNoBuilding;
  }
  // The mill's sound is started by BuildFarmView, beside the steading that
  // knows where the mill stands — on bare rock there is no mill to hear.
  //
  // The chunk itself is not needed past this point: its entities live in the
  // scene under island_root_, and holding the parse alive across the rest of
  // the load would keep a copy of the file for nothing.
  load_zone_.reset();
  return {};
}

Result<void> HearthfieldGame::LoadSystems(const aether::app::AppContext& ctx) {
  // THE ORDER IS THE DESIGN (spec §5.1). Plots first so a crop harvested this
  // step is in `in` for economy at the end of the same step; production before
  // orders so a finished item can fill one next step.
  // A NEW farm needs its mill and its starting corner; a loaded one brought
  // both with it. HAVING NO BUILDINGS is the marker — `unlocked == 0` was, and
  // could not stay, because a fresh world now owns its whole board by default
  // (see WorldView's constructor). Every real save has at least one building,
  // including a migrated v1.
  // A coop with no birds in it means one of two things and wants the same
  // answer to both: a brand-new farm, or a v2 save written before the coop
  // existed. Neither should leave the player with a building they can never
  // stock — the flock is fixed content, not a purchase (H5 plan §3h).
  if (world_.World().TheCoop().animals == 0) {
    world_.World().TheCoop().animals = content::kFlockSize;
  }
  if (world_.World().Buildings().empty()) {
    // The mill everyone starts with, standing where the authored `mill` locator
    // stands. Since H7 a building has a POSITION, so a new farm places its
    // first one rather than leaving it at the ring's corner.
    world_.World().AddBuilding(content::kMillKind, runtime::kStarterMillCell);
    world_.World().ThePurse().coin = economy::kStartingCoin;
    world_.World().TheLand().owned =
        std::min<U32>(economy::kStartingPlots, static_cast<U32>(grid_.Count()));
  }
  world_.AddSystem(plots_);
  world_.AddSystem(production_);
  world_.AddSystem(livestock_);
  world_.AddSystem(orders_);
  world_.AddSystem(economy_);
  // LAST: a placement spends coin, and economy settles the purse for the step
  // before it. Registration order is the design here as everywhere else.
  world_.AddSystem(placement_);
  if (sow_all_) {
    // UNLOCK THE BOARD FIRST, or this whole block plants ONE crop.
    //
    // A plot must be OWNED to be planted, and a new farm owns
    // `economy::kStartingPlots` of them — three of which already carry authored
    // crops. So `--sow` was adding exactly one mesh (239 -> 240 at grid 15)
    // while its comment below claimed a full board, and `hearthfield-board` has
    // been measuring a board with no crops on it since plot unlocking landed.
    // The flag predates that feature; a gameplay change invalidated a
    // measurement setup silently, which nothing was watching for.
    world_.World().TheLand().owned = static_cast<U32>(grid_.Count());
    // One tap per plot — the measurement wants a FULL board and hand-tapping
    // 225 of them is not a reproducible setup.
    //
    // STAGGERED, using offline_ticks to age each plot a slice more than the
    // next: plot 0 comes out ripe and the last is a fresh sprout, so one
    // screenshot shows the whole growth range AND both crop materials. Sowing
    // them all at once would show 225 identical stubs and prove nothing about
    // either.
    const auto grow = static_cast<U32>(runtime::TicksFromSeconds(
        content::CropById(content::kWheat).grow_seconds));
    const auto slice =
        static_cast<U32>(grow / std::max<Usize>(1, grid_.Count() - 1));
    for (Usize i = 0; i < grid_.Count(); ++i) {
      // EMPTY plots only, so --sow composes with a loaded farm instead of
      // harvesting the player's ripe crops as a side effect of measuring.
      if (world_.World().Plots()[i].state != runtime::PlotState::kEmpty) {
        continue;
      }
      // ALTERNATE THE CROP BY ROW, and that is a measurement decision as much
      // as a visual one. Wheat and corn are separate meshes, so a wheat-only
      // board is one batch where a played one is two — sowing only wheat would
      // under-report draws for the same reason sowing nothing under-reported
      // triangles (event:2026-08-28#20). Rows rather than a checker so the two
      // silhouettes sit in readable bands.
      const auto row = static_cast<U32>(i / grid_.columns);
      world_.Step(
          runtime::LatchedInput{
              .offline_ticks = i == 0 ? 0 : slice,
              .tap = true,
              .hovered = static_cast<runtime::PlotId>(i),
              .sow_crop = (row % 2 == 0) ? content::kWheat : content::kCorn},
          1.0f / 60.0f);
    }
  }
  if (open_screen_ == "barn") {
    screens_.Push(std::make_unique<BarnScreen>());
  } else if (open_screen_ == "shop") {
    screens_.Push(std::make_unique<ShopScreen>());
  } else if (open_screen_ == "orders") {
    screens_.Push(std::make_unique<OrdersScreen>());
  } else if (!open_screen_.empty()) {
    LogWarn("hearthfield: --screen {} is not barn|shop|orders", open_screen_);
  }

  // --travel: begin a crossing at load, so one can be CAPTURED. A headless run
  // has no keyboard, so without this the only way to see a flight is to press T
  // by hand — and the s6 load-time measurement needs a repeatable one.
  // It UNLOCKS the target first and says so: a dev flag that refused on price
  // would need --unlock too, and two flags to reach one screen is worse than
  // one flag that admits what it does.
  if (travel_target_ >= 0) {
    const auto target = static_cast<content::IslandId>(travel_target_);
    if (!content::IslandExists(target)) {
      return Fail(Errc::kInvalidArgument,
                  "--travel " + std::to_string(travel_target_) +
                      " is not an island; this build has " +
                      std::to_string(content::kIslands.size()));
    }
    if (!runtime::IsUnlocked(world_.World().Isles(), target)) {
      LogWarn("hearthfield: --travel unlocking {} for free (a dev flag)",
              content::kIslands[target].name);
      world_.World().Isles().unlocked |= (1u << target);
    }
    // The departure itself is latched in FixedUpdate, not here — see the note
    // there. What Load owns is validating the id and paying for the island.
  }

  LogInfo("hearthfield: {}x{} board, {} plots", grid_.columns, grid_.columns,
          grid_.Count());

  // THE LINT REPLACES THE GAME rather than preceding it: everything it needs is
  // built by now, and returning a failure from Load is what makes the process
  // exit non-zero without a window ever opening. main.cpp caps the run at one
  // frame so a CLEAN lint terminates too.
  if (ui_lint_) {
    return RunUiLint(ctx);
  }
  return {};
}

Result<scene::NodeId> HearthfieldGame::SpawnIsland(
    const aether::app::AppContext& ctx, content::IslandId id,
    std::optional<LoadedChunk>* zone_out) {
  const content::Island* island = content::FindIsland(id);
  if (island == nullptr) {
    return Fail(Errc::kInvalidArgument,
                "no island " + std::to_string(id) + " to spawn");
  }
  const scene::NodeId root = scene_.CreateNode(scene_.Root());
  if (!root.Valid()) {
    return Fail(Errc::kInvalidArgument, "the scene refused an island node");
  }
  scene_.SetLocalTransform(root, Transform{.position = island->world_pos});

  const auto spawn = [&](std::string_view path) {
    return LoadChunk(ctx, path, root);
  };

  if (auto ok = spawn(island->chunk); !ok) {
    scene_.DestroyNode(root);
    return std::unexpected(ok.error());
  }
  // The zone is HANDED BACK rather than used: the farm's view is built from it
  // by whoever called, and on a crossing that is arrival — the island being
  // left still owns the board until then, and there is only one view::Board.
  if (zone_out != nullptr) {
    zone_out->reset();
  }
  if (!island->zone.empty()) {
    auto loaded = spawn(island->zone);
    if (!loaded) {
      scene_.DestroyNode(root);
      return std::unexpected(loaded.error());
    }
    if (zone_out != nullptr) {
      *zone_out = std::move(*loaded);
    }
  }
  return root;
}

Result<void> HearthfieldGame::BuildFarmView(const aether::app::AppContext& ctx,
                                            const LoadedChunk& zone) {
  // THE THREE GO TOGETHER, and this is the only place that says so. Split
  // across Load and the arrival path they drifted immediately — the first
  // version of travel rebuilt the island and not the farm on it, so returning
  // to the hub gave a fence around no plots.
  if (auto built = board_.Create(ctx.resources, *materials_, scene_, grid_,
                                 island_root_);
      !built) {
    return built;
  }
  if (auto built =
          buildings_.Create(ctx.resources, *materials_, scene_, island_root_);
      !built) {
    return built;
  }
  if (auto built = steading_.Create(ctx.resources, *materials_, scene_,
                                    zone.chunk, zone.instance, island_root_);
      !built) {
    return built;
  }
  has_zone_ = true;
  // The mill's sound is positional and the mill has just moved — or arrived.
  audio_.Begin(ctx.audio, steading_.MillAt());
  return {};
}

Result<void> HearthfieldGame::RespawnNeighbours(content::IslandId resident,
                                                content::IslandId also_loaded) {
  if (neighbours_root_.Valid()) {
    scene_.DestroyNode(neighbours_root_);
    neighbours_root_ = scene::NodeId{};
  }
  const resources::WorldChunk chunk =
      NeighbourSilhouettes(resident, also_loaded);
  auto placed =
      scene::InstantiateChunk(scene_, scene_.Root(), chunk, *spawners_);
  if (!placed) {
    return std::unexpected(placed.error());
  }
  neighbours_root_ = placed->root;
  return {};
}

Result<void> HearthfieldGame::BeginTravel(const aether::app::AppContext& ctx,
                                          content::IslandId target) {
  if (transit_) {
    return Fail(Errc::kInvalidArgument,
                "already crossing — a second departure mid-flight would strand "
                "the island being left");
  }
  if (!content::IslandExists(target)) {
    return Fail(Errc::kInvalidArgument,
                "no island " + std::to_string(target) + " to travel to");
  }
  if (target == island_) {
    return Fail(Errc::kInvalidArgument, "already there");
  }
  if (!runtime::IsUnlocked(world_.World().Isles(), target)) {
    return Fail(Errc::kInvalidArgument,
                "that island is not yours yet — it costs " +
                    std::to_string(content::kIslands[target].unlock_coins) +
                    " coin");
  }

  // THE TARGET IS LOADED NOW, AT DEPARTURE, and the departed one freed on
  // ARRIVAL. Both islands are therefore resident for the length of the
  // crossing, which is the air lock's actual bargain — GEA §16.4.2 spends
  // memory to buy the absence of a loading screen. Flying toward an island that
  // is not there yet, or away from a hole in the sky, are both worse.
  // TIMED, because this number is what decides whether the load ever needs to
  // be asynchronous. The plan reached for `AsyncModelLoader` on the assumption
  // that a synchronous island load would stall the flight; that is a claim
  // about a duration, and this is the duration. Reported every crossing rather
  // than measured once, since it grows with whatever a satellite gains.
  const platform::Clock clock;
  const U64 began = clock.Now();
  std::optional<LoadedChunk> arriving_zone;
  auto arriving = SpawnIsland(ctx, target, &arriving_zone);
  if (!arriving) {
    return std::unexpected(arriving.error());
  }
  const F32 load_ms = platform::TicksToSeconds(clock.Now() - began) * 1000.0f;
  // Neither end is a silhouette while the crossing is in flight: both are real.
  if (auto ok = RespawnNeighbours(target, island_); !ok) {
    scene_.DestroyNode(*arriving);
    return ok;
  }

  // PARK THE FARM BEING LEFT, stamped with now. This is the other end of the
  // arrival's catch-up: without the stamp a returning farm would think no time
  // had passed, and everything on it would be frozen at the moment of leaving.
  // Done at DEPARTURE rather than arrival because the live world is about to be
  // overwritten by the island being entered.
  farms_.Park(island_, world_.World(), platform::WallClockSeconds(),
              grid_.columns);

  transit_ = Transit{.from = island_,
                     .to = target,
                     .elapsed = 0.0f,
                     .leaving = island_root_,
                     .zone = std::move(arriving_zone)};
  island_root_ = *arriving;
  island_ = target;

  // THE ROAM RECT MUST COVER BOTH ENDS FOR THE LENGTH OF THE FLIGHT, and this
  // is not a nicety: ClampFocus runs inside every Apply, so with the departed
  // island's box still in force the focus is dragged back the instant the
  // crossing tries to leave it, and the camera never gets off the island. The
  // union rather than "no bounds", because a crossing that overshoots should
  // still stop somewhere real.
  const content::CameraBounds from = content::kIslands[transit_->from].Roam();
  const content::CameraBounds to = content::kIslands[transit_->to].Roam();
  rig_.SetBounds(
      content::CameraBounds{.min_x = std::min(from.min_x, to.min_x),
                            .max_x = std::max(from.max_x, to.max_x),
                            .min_z = std::min(from.min_z, to.min_z),
                            .max_z = std::max(from.max_z, to.max_z)});

  // THE FARM IS NOT TORN DOWN HERE. It stands on the island being LEFT, which
  // is alive for the whole flight — so flying away from it shows a whole farm
  // receding rather than a fence around a bare field. The teardown belongs
  // beside the DestroyNode that makes it necessary, on arrival.
  build_kind_ = runtime::LatchedInput::kNoBuilding;
  LogInfo(
      "hearthfield: leaving {} for {} — loaded in {:.2f} ms of a {:.1f} s "
      "crossing ({:.3f}% of it)",
      content::kIslands[transit_->from].name,
      content::kIslands[transit_->to].name, load_ms, kTransitSeconds,
      100.0f * load_ms / (kTransitSeconds * 1000.0f));
  return {};
}

void HearthfieldGame::StepTravel(const aether::app::AppContext& ctx, F32 dt) {
  if (!transit_) {
    return;
  }
  transit_->elapsed += dt;
  const F32 t = std::clamp(transit_->elapsed / kTransitSeconds, 0.0f, 1.0f);
  // Smoothstep, so the crossing eases out of one island and into the next
  // rather than starting and stopping at full speed. Under a parallel
  // projection the whole sense of travel is the ground sliding past, and a
  // linear ramp reads as a cut with extra steps.
  const F32 eased = t * t * (3.0f - 2.0f * t);
  const Vec3 from = content::kIslands[transit_->from].world_pos;
  const Vec3 to = content::kIslands[transit_->to].world_pos;
  rig_.SnapFocus(
      Vec2{from.x + (to.x - from.x) * eased, from.z + (to.z - from.z) * eased});
  if (t < 1.0f) {
    return;
  }

  // ARRIVED. Free the island left behind — the first unload this game has ever
  // done — and put the departed one back on the horizon as a silhouette.
  //
  // THE FARM'S VIEW GOES FIRST, and the order is the whole of it. Its nodes
  // hang off the island about to be destroyed, and view::Steading holds raw
  // component POINTERS rather than ids — so tearing down after the destroy
  // would leave Update writing through freed storage, which is the failure the
  // editor's node-count check exists to catch one layer up.
  if (has_zone_) {
    board_.Clear(scene_);
    buildings_.Clear(scene_);
    steading_.Clear();
    has_zone_ = false;
  }
  if (transit_->leaving.Valid()) {
    scene_.DestroyNode(transit_->leaving);
  }
  const content::IslandId arrived = transit_->to;
  const std::optional<LoadedChunk> zone = std::move(transit_->zone);
  transit_.reset();

  // THE ARRIVING ISLAND'S FARM, out of its record and into the live world.
  //
  // ORDER MATTERS TWICE HERE. The sim swap comes before the view is built,
  // because view::Board sizes itself from `grid_` and the record decides how
  // big that is; and the whole thing comes after the teardown above, because
  // there is one view::Board and the island being left held it until a moment
  // ago.
  const runtime::Grid arriving_grid{.columns = farms_.Worked(arrived)
                                                   ? farms_.Columns(arrived)
                                                   : content::kDefaultColumns};
  const runtime::Landing landing =
      farms_.Enter(arrived, world_.World(), platform::WallClockSeconds(),
                   arriving_grid.Count());
  grid_ = arriving_grid;
  // THE CATCH-UP IS LATCHED, not applied here — the same path a relaunch takes
  // (runtime::Farms says why). It reaches the sim on the next fixed step, so a
  // farm left growing for a week arrives grown.
  latched_.offline_ticks = landing.offline_ticks;
  if (landing.what == runtime::Arrival::kFirstVisit) {
    // WHAT A NEW FARM STARTS WITH IS app/'S CALL, which is why Farms only
    // reports the arrival rather than deciding it. A satellite gets LAND and
    // nothing else: no starter mill, because building one is the point of
    // going, and no coin, because the purse is shared and crossed with you.
    world_.World().TheLand().owned = std::min<U32>(
        economy::kStartingPlots, static_cast<U32>(arriving_grid.Count()));
    LogInfo("hearthfield: {} has never been worked — {} plots and bare ground",
            content::kIslands[arrived].name, world_.World().TheLand().owned);
  } else {
    LogInfo("hearthfield: {} was left {} s ago — {} offline ticks to catch up",
            content::kIslands[arrived].name,
            landing.offline_ticks / runtime::kTicksPerSecond,
            landing.offline_ticks);
  }

  // The farm on the island just reached, if it has one.
  if (zone) {
    if (auto built = BuildFarmView(ctx, *zone); !built) {
      LogError("hearthfield: arrived at {} but its farm would not build — {}",
               content::kIslands[arrived].name, built.error().message);
    }
  }
  if (auto ok = RespawnNeighbours(arrived, arrived); !ok) {
    LogError(
        "hearthfield: arrived at {} but the horizon would not rebuild — {}",
        content::kIslands[arrived].name, ok.error().message);
  }
  // The roam rect follows the island, and ONLY the roam rect: zoom, quarter
  // turn and damping are the player's and survive the crossing, which is what
  // one persistent camera was chosen for (ADR-0142).
  rig_.SetBounds(content::kIslands[arrived].Roam());
  // `Isles().current` is NOT set here. The sim moved it the step the crossing
  // began — this is presentation catching up with a decision already made, and
  // writing world state from app/ is what game_api.hpp forbids.
  LogInfo("hearthfield: arrived at {}", content::kIslands[arrived].name);
}

void HearthfieldGame::LoadFarm(const aether::app::AppContext& ctx) {
  if (no_save_) {
    return;
  }
  // UserDataDir REFUSES on the web — deliberately, because a browser has no
  // answer that is merely a path (it needs IDBFS) and a non-persisting one is
  // worse than saying so. The game stays playable there; it simply forgets.
  // Said once, here, instead of at every autosave. ANDROID USED TO BE IN THIS
  // SENTENCE and no longer is: ADR-0119 derives its internal storage from
  // /proc, so the platform this game exists to prove remembers on actually
  // does.
  auto dir = platform::UserDataDir(kAppName);
  if (!dir) {
    LogWarn(
        "hearthfield: nowhere to save on this platform ({}) — the farm "
        "will not persist",
        dir.error().message);
    return;
  }
  save_path_ = *dir + "/" + std::string(kSaveFile);
  if (!ctx.assets.Exists(save_path_)) {
    LogInfo("hearthfield: a new farm ({})", save_path_);
    return;
  }

  auto bytes = ctx.assets.Read(save_path_);
  if (!bytes) {
    LogError("hearthfield: cannot read {} — {}", save_path_,
             bytes.error().message);
    return;
  }
  auto farm = runtime::DecodeSave(*bytes);
  if (!farm) {
    // LOUD, and the farm is left alone rather than overwritten: a save that
    // will not parse is still the player's, and the next autosave must not be
    // what destroys it. Hence no_save_ from here on.
    LogError("hearthfield: {} will not load — {}. NOT overwriting it.",
             save_path_, farm.error().message);
    save_path_.clear();
    return;
  }

  // THE SAVE SIZES THE BOARD (plan §3c): --grid describes a new farm only, and
  // trusting it over the file would truncate or pad the player's plot table.
  grid_.columns = farm->columns;
  world_ = runtime::GameWorld(/*seed=*/1, farm->plots.size());
  runtime::RestoreFarm(world_.World(), *farm);

  // The dormant islands back into the container that owns them. Indexed by id,
  // so the parallel arrays the file carries become the sparse table `Farms`
  // keeps — a record for an island this build lacks was already dropped by the
  // decoder, so everything here indexes kIslands safely.
  {
    std::vector<runtime::SavedFarm> records(content::kIslands.size());
    std::vector<U8> present(content::kIslands.size(), 0);
    for (Usize i = 0; i < farm->dormant.size(); ++i) {
      const content::IslandId id = farm->dormant_ids[i];
      records[id] = farm->dormant[i];
      present[id] = 1;
    }
    farms_.Restore(std::move(records), std::move(present));
  }
  saved_digest_ = farms_.Digest(world_.World());

  // The clock is read HERE and only here — the one layer allowed to (spec §4.2)
  // — and the result enters the sim as latched input on the next fixed step,
  // exactly like a button press. `OfflineTicksBetween` clamps both directions.
  const I64 now = offline_override_ >= 0 ? farm->saved_at + offline_override_
                                         : platform::WallClockSeconds();
  latched_.offline_ticks = runtime::OfflineTicksBetween(farm->saved_at, now);
  LogInfo(
      "hearthfield: loaded {} plots at tick {}, away {} s -> {} offline ticks",
      farm->plots.size(), farm->tick, now - farm->saved_at,
      latched_.offline_ticks);
}

void HearthfieldGame::SaveFarm(const aether::app::AppContext& ctx) {
  if (save_path_.empty()) {
    return;
  }
  runtime::SavedFarm farm = runtime::CaptureFarm(
      world_.World(), platform::WallClockSeconds(), grid_.columns);
  // THE ISLANDS NOT BEING STOOD ON, appended (save v6). The live farm above is
  // the one the file's v1..v5 block describes, so the island it is ON is
  // skipped here — its record in `farms_` is whatever it was when last parked,
  // which is older than the world in front of the player.
  for (content::IslandId id = 0; id < content::kIslands.size(); ++id) {
    if (id == island_ || !farms_.Worked(id)) {
      continue;
    }
    farm.dormant_ids.push_back(id);
    farm.dormant.push_back(farms_.Records()[id]);
  }
  const std::vector<Byte> bytes = runtime::EncodeSave(farm);
  // Atomic (temp + rename), so a force-quit mid-write cannot leave a torn file
  // — the previous save survives intact. That is what makes autosaving safe.
  if (auto written = ctx.assets.WriteBytes(save_path_, bytes); !written) {
    LogError("hearthfield: cannot write {} — {}", save_path_,
             written.error().message);
    return;
  }
  // THE WHOLE ARCHIPELAGO, matching the check in Update. Digesting only the
  // live farm here would make travel look like "nothing changed" the moment
  // the player came home to an island whose record had moved.
  saved_digest_ = farms_.Digest(world_.World());
}

std::optional<Vec3> HearthfieldGame::GroundAt(
    const aether::app::AppContext& ctx, Vec2 pixels) const {
  const Size fb = ctx.window.FramebufferSize();
  if (!have_camera_ || fb.width == 0 || fb.height == 0) {
    return std::nullopt;
  }
  // The Y flip lives in ResolvePointer too and for the same reason:
  // framebuffer pixels run +Y down, NDC runs +Y up.
  const Vec2 ndc{(2.0f * pixels.x / static_cast<F32>(fb.width)) - 1.0f,
                 1.0f - (2.0f * pixels.y / static_cast<F32>(fb.height))};
  const Ray3 ray = ScreenRay(last_camera_, ndc,
                             Viewport{.width = fb.width, .height = fb.height});
  if (std::abs(ray.direction.y) < 1.0e-5f) {
    return std::nullopt;  // parallel to the ground; there is no such point
  }
  const F32 t = -ray.origin.y / ray.direction.y;
  if (t < 0.0f) {
    return std::nullopt;  // the ground is behind the camera
  }
  return ray.At(t);
}

void HearthfieldGame::ResolvePointer(const aether::app::AppContext& ctx) {
  latched_.hovered = runtime::kNoPlot;
  const Size fb = ctx.window.FramebufferSize();
  if (!have_camera_ || fb.width == 0 || fb.height == 0) {
    return;
  }
  const Vec2 pointer = ctx.input.PointerPosition();
  // The Y flip lives HERE and nowhere else: framebuffer pixels run +Y down and
  // NDC runs +Y up. A mirrored ray picks a plot reflected about the board's
  // centre, which looks like bad tolerance rather than like a sign error.
  const Vec2 ndc{(2.0f * pointer.x / static_cast<F32>(fb.width)) - 1.0f,
                 1.0f - (2.0f * pointer.y / static_cast<F32>(fb.height))};

  const Ray3 ray = ScreenRay(last_camera_, ndc,
                             Viewport{.width = fb.width, .height = fb.height});
  if (const std::optional<runtime::PlotId> hit =
          runtime::PickPlot(grid_, world_.Snapshot().plots, ray)) {
    latched_.hovered = *hit;
  }
}

// The first order the barn can actually satisfy, or kNoSlot. app/'s job: it
// sees everything (AGENTS.md law 3), and the script may not.
aether::U8 HearthfieldGame::FillableSlot() const {
  const runtime::Barn& barn = world_.World().TheBarn();
  const runtime::OrderBoard& board = world_.World().Board();
  for (Usize slot = 0; slot < runtime::kOrderSlots; ++slot) {
    const runtime::Order& order = board.slots[slot];
    if (order.active && barn.Of(order.item) >= order.count) {
      return static_cast<aether::U8>(slot);
    }
  }
  return runtime::LatchedInput::kNoSlot;
}

void HearthfieldGame::Update(const aether::app::AppContext& ctx, F32 dt) {
  last_dt_ = dt;
  // THE RIG GETS EXACTLY ONE FRAME PER FRAME, in both branches. Its eases are
  // integrated per call, so applying it only on the frames that produce input
  // would leave a quarter turn frozen between autopilot turns — the smoothing
  // would exist and never run.
  view::CameraIntent intent;
  Vec2 anchor_world{};
  bool has_anchor = false;
  if (autopilot_) {
    // The camera turn is PRESENTATION and stays on the render frame; the script
    // itself does not — see FixedUpdate.
    if (frames_ > 0 && frames_ % kAutopilotPeriod == 0) {
      intent.rotate_steps = 1;
    }
  } else {
    intent = camera_input_->Poll(ctx.input, ui_);

    // A SCREEN IS MODAL, AND THE KEYBOARD HAS TO BE TOLD SEPARATELY. The
    // pointer, touch and wheel paths gate on `WantsPointer`, which the scrim
    // now claims for the whole canvas — but WASD pan and Q/E turns are not
    // pointer input and were still driving the board under an open panel.
    //
    // `WantsPointer` is the WRONG signal for them: it is also true while the
    // cursor merely rests on the HUD tabs, so gating keys on it would stop
    // panning whenever the mouse sat at the top of the screen. Whether a screen
    // is modal is a FLOW question, and screens.hpp says flow lives here.
    //
    // POLLED FIRST AND DISCARDED AFTER, never skipped: the gesture recognizer
    // needs every frame, including the empty ones, to see a finger lift.
    if (!screens_.Empty()) {
      intent = view::CameraIntent{};
    }

    // THE ONE PART THAT NEEDS A VIEWPORT, which is why it is here and not in
    // the rig: the ground point under the zoom anchor. hf/runtime/grid.hpp
    // makes the same split for picking — "a viewport belongs to whoever built
    // the ray, which is app/".
    if (intent.has_zoom_anchor) {
      const std::optional<Vec3> ground = GroundAt(ctx, intent.zoom_anchor);
      if (ground) {
        anchor_world = Vec2{ground->x, ground->z};
        has_anchor = true;
      }
    }

    // A hover under a panning cursor would sit lit on a plot the player is not
    // pointing at, so resolve it only when the camera is still — and never
    // under an open screen, where a plot lighting up BEHIND the scrim invites
    // a tap the panel is there to take.
    if (camera_input_->Panning() || !screens_.Empty()) {
      latched_.hovered = runtime::kNoPlot;
    } else {
      ResolvePointer(ctx);
    }
    // Latched, never read inside the fixed step: FixedUpdate runs 0..N times a
    // frame and an edge read there is dropped on a zero-step frame. `tap` is
    // already arbitrated against dragging — see view/camera_input.
    latched_.tap = latched_.tap || intent.tap;

    // ---- build mode -------------------------------------------------------
    //
    // The SAME arbitrated tap the board uses, which is the whole reason this
    // was cheap: a drag that pans the camera does not place a building for
    // exactly the reason it does not select a plot.
    if (build_kind_ != runtime::LatchedInput::kNoBuilding) {
      // Never while panning, for the reason the hover is suppressed above: a
      // ghost under a dragging finger sits on ground the player is not
      // pointing at.
      const std::optional<Vec3> ground =
          camera_input_->Panning() ? std::nullopt
                                   : GroundAt(ctx, ctx.input.PointerPosition());
      // THE GHOST IS PARKED, not hidden, when the pointer is not over the ring.
      // It follows the pointer when there IS one — but a touch device has no
      // pointer until a finger lands, so hiding until then would mean entering
      // build mode showed nothing at all and left the player guessing what the
      // mode had done. `build_cell_` keeps the last legal-looking cell, and
      // starts at a spot beside the field.
      if (const std::optional<runtime::BuildCell> over =
              ground ? ring_.CellAt(*ground) : std::nullopt) {
        build_cell_ = *over;
      }
      const bool legal = runtime::CanPlace(ring_, build_kind_, build_cell_,
                                           world_.World().Buildings()) ==
                         runtime::PlaceRefusal::kOk;
      buildings_.ShowGhost(scene_, ring_, build_kind_, build_cell_, legal,
                           /*visible=*/true);
      // A tap on legal ground commits. On illegal ground it does nothing at
      // all rather than leaving build mode — the player is mid-decision, and
      // dropping them back to farming because they brushed the field would be
      // the mode cancelling itself on a near miss.
      if (latched_.tap && legal) {
        latched_.place_kind = build_kind_;
        latched_.place_cell = build_cell_;
        build_kind_ = runtime::LatchedInput::kNoBuilding;
      }
      // The tap is SPENT either way: it must not also plant a crop under the
      // ghost on the same frame.
      latched_.tap = false;
    }
    // B enters build mode, and leaves it. Escape's meaning is already taken
    // (it closes screens and quits), so the toggle is its own key.
    if (ctx.input.JustPressed(platform::Key::kB)) {
      build_kind_ = build_kind_ == runtime::LatchedInput::kNoBuilding
                        ? content::kMillKind
                        : runtime::LatchedInput::kNoBuilding;
      if (build_kind_ == runtime::LatchedInput::kNoBuilding) {
        buildings_.ShowGhost(scene_, ring_, content::kMillKind, build_cell_,
                             false, /*visible=*/false);
      }
    }

    // H3's number keys are GONE — the screens do all of this now (H4). What
    // remains is opening them, which is flow rather than a game action and so
    // is not latched.
    if (ctx.input.JustPressed(platform::Key::k1)) {
      screens_.Push(std::make_unique<BarnScreen>());
    }
    if (ctx.input.JustPressed(platform::Key::k2)) {
      screens_.Push(std::make_unique<ShopScreen>());
    }
    if (ctx.input.JustPressed(platform::Key::k3)) {
      screens_.Push(std::make_unique<OrdersScreen>());
    }
    // T CROSSES TO THE NEXT ISLAND YOU OWN. A key rather than a button
    // because the shop entry that ought to offer this is still deferred (s4's
    // agreed split), and a crossing with no way to start it is untestable by a
    // person. It cycles, so with only the hub unlocked it does nothing at all.
    if (ctx.input.JustPressed(platform::Key::kT) && !transit_) {
      for (Usize step = 1; step < content::kIslands.size(); ++step) {
        const auto next = static_cast<content::IslandId>(
            (island_ + step) % content::kIslands.size());
        if (runtime::IsUnlocked(world_.World().Isles(), next)) {
          // THROUGH THE LATCH, like the shop's button: the key is a shortcut
          // for the same verb, not a second path into the world.
          latched_.travel_to = next;
          break;
        }
      }
    }
    close_requested_ = ctx.input.JustPressed(platform::Key::kEscape);
    ui_.SetScroll(ctx.input.ScrollDelta());
  }
  const Size fb = ctx.window.FramebufferSize();
  rig_.Apply(intent, anchor_world, has_anchor,
             Vec2{static_cast<F32>(fb.width), static_cast<F32>(fb.height)}, dt);
  // AFTER Apply, so the crossing overrides the player rather than fighting it:
  // a flight carries the camera, and input landing on top of that would let a
  // drag stall the transit halfway across the sky. The RENDER dt, because a
  // crossing is presentation — it changes no sim state until it lands.
  StepTravel(ctx, dt);
  ++frames_;
  // Autosave: on the clock, and only when something actually happened. An idle
  // farm writes nothing, which is what keeps a once-a-minute write from being a
  // once-a-minute write.
  since_save_ += dt;
  if (since_save_ >= kAutosaveSeconds) {
    since_save_ = 0.0f;
    if (farms_.Digest(world_.World()) != saved_digest_) {
      SaveFarm(ctx);
    }
  }
  rig_.Write(*orbit_, *projection_);
  scene_.Update(dt);
}

void HearthfieldGame::FixedUpdate(const aether::app::AppContext& ctx, F32 dt) {
  // ONE BEAT PER FIXED STEP, and it must be here rather than in Update. Keyed
  // off the render frame, a beat could be overwritten before any fixed step
  // consumed it — FixedUpdate runs 0..N times a frame — so the session played
  // depended on the frame rate, and a digest over it would have been a
  // different number on a different machine. That is the input-latch rule from
  // AGENTS.md applied to a scripted input source rather than to a key.
  if (autopilot_ && script_beat_ <= runtime::kScriptBeats) {
    latched_ = runtime::ScriptBeat(
        script_beat_,
        runtime::ScriptContext{
            .plot_count = static_cast<U32>(grid_.Count()),
            // Live state, so app/ resolves it (law 3): a script naming a fixed
            // slot would be shipping whatever the RNG posted there.
            .fillable_slot = FillableSlot(),
            .order_refresh =
                runtime::TicksFromSeconds(orders::kRefreshSeconds)});
    ++script_beat_;
  }
  // --travel is applied AFTER the script beat and not in Load, because
  // ScriptBeat ASSIGNS the whole latch rather than editing it — so a departure
  // set at load was silently wiped by the first beat under --autopilot, and the
  // flag did nothing at all in exactly the configuration the cost oracle uses.
  // hearthfield-crossing is what caught that; it went red the moment travel
  // moved onto the latch, which is the entire reason it exists.
  if (travel_target_ >= 0) {
    latched_.travel_to = static_cast<content::IslandId>(travel_target_);
    travel_target_ = -1;
  }
  const runtime::EventList& events = world_.Step(latched_, dt);
  // THE SIM DECIDED THE PLAYER MOVED; app/ turns that into a crossing. The
  // decision is the sim's because `Isles().current` is world state and is in
  // the digest — between s5 and s7 app/ set it directly, which worked perfectly
  // and would have silently stopped a session reproducing (game_api.hpp's
  // opening comment, made real).
  for (const runtime::GameEvent& event : events) {
    if (const auto* left = std::get_if<runtime::Departed>(&event)) {
      if (auto ok = BeginTravel(ctx, left->to); !ok) {
        LogWarn(
            "hearthfield: the sim departed for {} but the crossing would "
            "not start — {}",
            content::kIslands[left->to].name, ok.error().message);
      }
    }
  }
  // Every ONE-SHOT verb is cleared; `sow_crop` is not, because it is a mode the
  // player chose rather than an action they took.
  latched_.tap = false;
  latched_.offline_ticks = 0;
  latched_.queue_recipe = runtime::LatchedInput::kNoRecipe;
  latched_.fill_slot = runtime::LatchedInput::kNoSlot;
  latched_.buy_land = false;
  latched_.buy_barn = false;
  latched_.buy_item = runtime::LatchedInput::kNoItem;
  latched_.buy_count = 0;
  latched_.feed_coop = false;
  latched_.unlock_island = runtime::LatchedInput::kNoIsland;
  latched_.travel_to = runtime::LatchedInput::kNoIsland;
  // A placement is a one-shot like the rest: leaving the kind set would rebuild
  // on the next step, and on every step after it.
  latched_.place_kind = runtime::LatchedInput::kNoBuilding;
}

RenderFrame HearthfieldGame::Extract(const aether::app::AppContext& ctx) {
  const rhi::FrameStats stats = ctx.device.GetFrameStats();
  if (const auto gpu = stats.GpuFrameMs()) {
    gpu_ms_total_ += *gpu;
    ++gpu_samples_;
  }
  draw_calls_ = stats.draw_calls;
  peak_draw_calls_ = std::max(peak_draw_calls_, draw_calls_);

  const runtime::ViewSnapshot& snap = world_.Snapshot();
  // Only where the farm is — these mirror the sim onto nodes that exist only on
  // an island with a zone. The sim itself keeps stepping regardless, which is
  // what makes leaving the farm no different from closing the game.
  if (has_zone_) {
    board_.Update(scene_, grid_, snap);
    buildings_.Update(scene_, ring_, snap, last_dt_);
    steading_.Update(snap);
  }

  // THE RENDER dt, not the fixed one. Rain is presentation and lives nowhere in
  // the world state, so it advances with the frame rather than with the farm
  // clock (H5 plan §3e). `--rain` forces the sky on for a capture.
  weather_.Update(last_dt_, force_rain_ || snap.raining);

  // WETNESS ON THE SOIL — check 8's first half. One value for one surface, on
  // every soil tile: the field is uniform per surface by design, and a collider
  // per plot would be 225 sweeps a drop to buy a mottled board.
  //
  // BEFORE BuildRenderFrame, which is the whole point. Written after it, the
  // value lands on components the frame has already been extracted from — and
  // it does NOT simply arrive one frame late, it never arrives at all.
  if (has_zone_) {
    board_.SetWetness(scene_, weather_.Wetness());
  }
  scene_.Update(0.0f);
  RenderFrame frame =
      scene_.BuildRenderFrame(Viewport{.width = ctx.render_size.width,
                                       .height = ctx.render_size.height},
                              ctx.ShowDebugBounds(), &ctx.jobs);
  // One hard key light and no fill leaves every shadow side on the engine's
  // generic default (content::kAmbient says why this is a constant, not an
  // IBL).
  frame.environment.ambient = content::kAmbient;
  last_camera_ = frame.view.camera;
  have_camera_ = true;
  weather_.Emit(frame.items, frame.view.camera);

  if (audio_system_ != nullptr) {
    // The listener at the camera's TARGET, not its eye (H5 plan §3g). Under a
    // parallel projection the eye's distance renders identically at any value,
    // so measuring from it would put every sound the same distance away.
    audio_.Update(*audio_system_,
                  view::ListenerFor(orbit_->pivot, orbit_->CurrentYaw(),
                                    orbit_->CurrentPitch()),
                  snap, last_dt_);
  }
  // Recorded because the draw count alone is ambiguous: a board wider than the
  // view is partly culled, and "13 draws" would then be flattering rather than
  // measured. This says how many meshes those draws actually carried.
  meshes_ = static_cast<U32>(frame.meshes.size());
  visible_meshes_ = frame.visible_mesh_count;
  return frame;
}

RenderFrame HearthfieldGame::BuildOverlay(const aether::app::AppContext& ctx) {
  const Size fb = ctx.window.FramebufferSize();
  // The ABSTRACT pointer, not the mouse: check 7 says "usable by touch", and
  // on a phone the screens are the first thing a finger lands on (ADR-0090).
  // The white 1x1 is NOT optional: `ui` makes no GPU resources of its own, so
  // every Panel and every button background is a textured quad and passing an
  // invalid handle draws exactly nothing — labels appear, panels do not, and it
  // looks like a layout bug rather than a missing texture.
  ui_.BeginFrame(
      ctx.input.PointerPosition(), ctx.input.IsPointerDown(), fb, font_.get(),
      ctx.resources.WhiteTexture() ? ctx.resources.WhiteTexture()->Handle()
                                   : TextureHandle{},
      ctx.reference_size);

  // THE LOADING SCREEN IS THE WHOLE OVERLAY, and returning here is not a
  // shortcut: the loop skips Update, Extract and the pipeline on a loading
  // frame precisely because the world is half-built, so the HUD, the tabs and
  // the screens below would all be reading state that does not exist yet.
  if (load_phase_ != LoadPhase::kDone) {
    const aether::app::LoadProgress progress = LoadingProgress();
    view::DrawLoading(ui_, progress.fraction, progress.label);
    return ui_.EndFrame();
  }

  const runtime::ViewSnapshot& snapshot = world_.Snapshot();
  view::DrawHud(ui_, snapshot, panel_tex_.get());

  BuildTabs();

  ScreenCtx screen_ctx{.api = runtime::GameApi(latched_),
                       .snapshot = &snapshot,
                       .close_requested = close_requested_,
                       .panel = panel_tex_.get()};
  screens_.ApplyPending(screen_ctx);
  screens_.HandleInput(screen_ctx);
  screens_.Animate(screen_ctx, last_dt_);
  screens_.BuildUi(screen_ctx, ui_);
  close_requested_ = false;
  return ui_.EndFrame();
}

void HearthfieldGame::Unload(const aether::app::AppContext& ctx) {
  // The one that makes check 4 work on a clean quit; the autosave above is for
  // every other way a game ends.
  SaveFarm(ctx);

  // The measurement (check 2) is printed rather than eyeballed, because spec
  // risk 3 reopens on a NUMBER and nothing else.
  const F64 gpu =
      gpu_samples_ > 0 ? gpu_ms_total_ / static_cast<F64>(gpu_samples_) : 0.0;
  // The chain, in one line, so a played session can be checked without a HUD
  // (which is H4's). This is how H3's end-to-end run is verified.
  // From the WORLD, not the snapshot. A snapshot is built inside a fixed step,
  // and a fast headless run can take none in twenty frames (the accumulator is
  // real-time — lantern's --demo records the same trap), so reporting from it
  // printed a farm of all zeros that looked like a broken game rather than an
  // unstepped one.
  const runtime::WorldView& farm = world_.World();
  U32 milling = 0;
  for (const runtime::Building& building : farm.Buildings()) {
    milling += building.queued;
  }
  // THE STAMPED DIGEST (spec check 9). A world digest, not a pixel one: it is
  // FNV-1a over integers field by field, so it means the same on every machine
  // — where a capture digest is calibrated to one toolchain and one GPU. It
  // reproduces only when the loop is DETERMINISTIC, which `--capture` turns on
  // (dev_flags.hpp); a plain `--frames` run takes a machine-speed-dependent
  // number of fixed steps and therefore reaches a different beat, which is why
  // the oracle is the headless case and this line is the demo.
  LogInfo("hearthfield digest: {:#018x} at tick {} (script beat {} of {})",
          farm.Digest(), farm.Now(), script_beat_, runtime::kScriptBeats);
  LogInfo(
      "hearthfield chain: {} coin, barn {}/{}, {} milling, {} of {} plots "
      "owned",
      farm.ThePurse().coin, farm.TheBarn().Total(), farm.TheBarn().capacity,
      milling, farm.TheLand().owned, grid_.Count());
  for (Usize slot = 0; slot < runtime::kOrderSlots; ++slot) {
    const runtime::Order& order = world_.World().Board().slots[slot];
    if (order.active) {
      LogInfo("hearthfield order {}: {} x{} for {} coin", slot,
              content::ItemById(order.item).name, order.count, order.reward);
    }
  }
  LogInfo(
      "hearthfield weather: {} colliding drops + {} in the sky, soil wetness "
      "{:.3f}; coop {} birds, {} eggs, {}",
      weather_.DropCount(), weather_.SkyDropCount(), weather_.Wetness(),
      farm.TheCoop().animals, farm.TheBarn().Of(content::kEgg),
      farm.TheCoop().Fed(farm.Now()) ? "fed" : "hungry");
  LogInfo(
      "hearthfield: {} plots, {} meshes ({} visible), {} materials, draws "
      "last={} peak={}, gpu {:.3f} ms avg over {} frames",
      grid_.Count(), meshes_, visible_meshes_,
      materials_ == nullptr ? 0 : materials_->CachedCount(), draw_calls_,
      peak_draw_calls_, gpu, gpu_samples_);
}

}  // namespace hearthfield::app
