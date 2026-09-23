# games/ — building games on aether

> Read the root [`AGENTS.md`](../AGENTS.md) first. `games/coin_rush/` is the worked
> reference for every rule below — when unsure, read it there first. Scale by adding
> units, never by fattening existing ones.

## The one rule

A game is a **downstream application**: it depends *down* into `aether::` libraries
and **never edits the engine to suit itself**. Missing engine capability = an engine
change (own plan + review under `engine/` or `docs/plans/`); leave a TODO referencing
the plan and use the closest thing that exists.

## Layered layout

`games/<name>/`, one static library per layer, one executable in `app/`:

```
games/<name>/
  app/        # composition root ONLY: main.cpp, config, wiring, screen stack. Tiny.
  features/   # gameplay modules (movement/, combat/, …) — the layer that grows
  runtime/    # shared foundation: GameWorld + fixed step, event bus, ViewSnapshot,
              #   GameApi, save/load, replay
  view/       # ALL presentation: extract, hud, vfx glue, camera, audio cues
  content/    # declarative data + validation (levels, items, tuning)
  assets/     # authored art/audio/fonts (gen_assets → typed constants)
  shaders/    # this game's OWN .sc sources, compiled to its own output
  generated/  # gen_assets / gen_content output — gitignored
  tests/      # per-feature tests + the game's CTest target (<name>_tests)
```

### Public vs private headers — ONE include root per game

The engine has a single `${AETHER_INCLUDE_DIR}` for every module, not one per
module. A game is the same kind of thing, so it gets one too:

```
games/<name>/
  include/<prefix>/<layer>/   # PUBLIC: every layer's headers, one root
      <prefix>/features/<f>/  #   a feature keeps the `features/` segment
  <layer>/src/                # PRIVATE: implementation + headers only that layer uses
  <layer>/tests/              # optional, per-layer
```

`set(<GAME>_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")` at the game root;
every layer's `target_include_directories` points at it. **A layer's public
surface is a property of the GAME, not of the directory it is built from** —
which is the whole argument for one root, and why per-layer `include/` dirs were
consolidated on 2026-08-20. The `#include` lines did not change: only the root
moved, because the layer name was already inside the path.

Three rules, and the reasons they are rules rather than taste:

- **`app/` has no `include/`.** Dependency law 3 is *"nothing depends on `app`"*, so
  every one of its headers is private by definition and sits beside its `.cpp`.
  Giving `app/` a public directory would advertise a surface no one may consume.
- **A header goes in `include/` when another layer includes it, and in `src/`
  otherwise.** Unconsumed-but-declared is fine for a feature's `events.hpp` or
  `tuning.hpp` — those are the contract dependency law 1 is written around, and a
  contract with no subscriber yet is still a contract.
- **No loose headers at the game root, and no per-layer `include/`.**
  `coin_rush` had a `tuning.hpp` at the root until 2026-08-20, reached by putting
  the root on the include path — the one file in any game that was neither public
  nor private. It had exactly one consumer and moved into `app/`.
- **A public header is always `<prefix>/<layer>/…`** — at least two segments. A
  header dropped straight into `include/<prefix>/` belongs to no layer, so nothing
  can say who owns it.

`game_layer_layout` enforces all three.

The per-layer version of this was followed by all three games and written down
nowhere, which is how `tideworn` came to spell its feature includes
`include/tideworn/<feature>/` without the `features/` segment.
Practised-but-unwritten is how a layout drifts.

- **A game owns its authored assets and expressive shaders** (materials, post-FX,
  lighting looks): its own `assets/` (`gen_assets`) and `shaders/` with its own
  output dir. Never borrow sandbox assets or `sandbox_shaders`; duplicating a shader
  source into the game is expected and correct.
- **Exception — the core sprite/quad shader is engine-owned** (embedded behind the
  RHI seam; `engine/rhi/shaders/`; `Renderer::Create(device)` pulls
  `rhi::DefaultSprite*Shader()`) — games render out of the box, no shader setup.
  Default textures too: 1×1 white + "missing" checkerboard via
  `ResourceManager::WhiteTexture()`/`MissingTexture()` + `LoadOrPlaceholder`, so a
  bad asset path degrades visibly.
- Small games may collapse `view/` into fewer files, but the **layer skeleton,
  per-feature CMake targets, event bus, headless-sim rule, and replay support are
  mandatory from day one** — cheap now, brutal to retrofit. Don't front-load
  features you don't need; grow by adding slices.

## The dependency law

1. **`features` depend on `runtime`, and on each other ONLY via events + shared
   component data** — never `#include` or call another feature directly.
2. **`view` depends only on `runtime`'s snapshot/event types** — never on features.
3. **`app` sees everything; nothing depends on `app`.**
4. **`runtime` + `features` compile and run headless — STRICTLY, since
   2026-08-20.** A sim library links `aether::scene_core`, whose entire link
   closure is `{core, effects}`: no `resources`, no `rhi`, no bgfx. It is no longer
   a pragmatic boundary that a sim merely *runs* headless while transitively
   pulling a renderer — `game_sim_layers_link_scene_core` fails the build if a sim
   layer names `aether::scene` or `aether::render`.

   Do NOT reimplement engine collision in game code to dodge this: the queries live
   in `scene_core` precisely so you do not have to.

### Enforce the law in CMake, not in review

Each lib exposes *its own* directory as a `PUBLIC` include dir and links only its
allowed deps — an illegal cross-layer `#include` is a compile error, an illegal
symbol a link error. Target graph (concrete files in `coin_rush/`):

```
<name>_content       deps: aether::core
<name>_runtime       deps: aether::core scene worldsim surface … + <name>_content
                          (NO aether::render, NO device)
<name>_feat_<x>      deps: <name>_runtime aether::core   (+ sim libs it needs)
<name>_view          deps: <name>_runtime aether::render rhi ui resources effects …
                          (consumes snapshot+events; NOT any feature)
<name> (exe, app/)   deps: <name>_runtime <name>_feat_* <name>_view
                          <name>_content aether::app
```

Back it with **grep-style seam tests** (mirror `engine/tests/CMakeLists.txt`): no sim
lib links `aether::render`; no `features/<x>` source includes `view/` or a sibling
feature. Plus a **headless-sim smoke test** stepping `GameWorld` with no device.

## Feature slice template

```
features/<feature>/
  components.hpp     # plain data only (no logic, no side effects)
  system.{hpp,cpp}   # pure step: Step(world_view, input, events_in) -> events_out
                     #   NO rendering/audio/particle side effects
  events.hpp         # events this feature emits / consumes
  tuning.hpp         # feature-local constants (feel lives WITH its feature)
  tests/             # unit tests (headless, deterministic)
```

A system reads shared state through a **narrow query seam** and writes back shared
component data + events (coin_rush movement takes `WorldQuery` + `WeatherQuery`, so
it tests against trivial fakes). Features read another feature's output via shared
component data, never by calling that system.

## The event bus — the inter-feature & sim→view contract

A frame-buffered `std::vector<GameEvent>` (a `std::variant` of small structs) is THE
way systems talk to each other and to `view`. Systems emit during the fixed step in
an **explicit registration order set by `app`** (no priority magic); after the step,
`view` drains the events to react (particles, camera shake, audio, flashes). With
several systems, route reactions through events, not returned result structs.

## Sim / view split

- The **fixed step lives entirely in `runtime` + `features`** and produces an
  immutable **`ViewSnapshot`** (camera, poses, HUD data — the HUD struct is a
  member) plus the frame's events.
- **`Extract`/`BuildOverlay` and all render passes live in `view/`**, consuming ONLY
  the snapshot + drained events. Pass order is composed once in `Game::BuildPipeline`
  (`aether/app/render_pipeline.hpp`; coin_rush is the worked example). The HUD draws
  from the snapshot struct (`DrawHud(ui, snapshot.hud)`), never from game state.
  **A pass never walks the live `Scene`** — under `pipelining` the extract worker is
  walking it concurrently. Second view (minimap, extra camera): build it in
  `Extract`, ring it, index with `FrameContext::frame_index` — recipe in
  [`engine/app/AGENTS.md`](../engine/app/AGENTS.md).
- **No screen↔game friendship.** Screens (in `app/`) drive flow through a narrow
  **`GameApi`** (defined in `runtime`) with exactly the verbs needed (`StartLevel`,
  `PushPause`, `NextLevel`, `RequestQuit`, + read-only state).

## Determinism & replay (a pillar, not an add-on)

The fixed step is **pure and deterministic**:
- **No wall clock** in the step (use the fixed `dt`); **no unseeded RNG** — one
  seeded world RNG owned by `GameWorld`.
- **All edge inputs latched per fixed step** (via `ctx.sim_input`; rule below) — a
  step is a pure function of `(world, LatchedInput, dt)`.
- Record `LatchedInput` per step → **input-script replay** reproduces a run exactly;
  replays are a regression oracle alongside the capture harness. The headless capture
  autopilot IS a canned input script (an `InputSource`) — live keyboard, replay-file,
  and autopilot are interchangeable sources feeding the same `GameWorld.Step`.

### The fixed-timestep input-latch rule (do not skip)

`app::Run` polls input once per render frame but runs `Game::FixedUpdate` **0..N
fixed steps per frame — including zero**, so edge input (`JustPressed`) read *inside*
the fixed step is dropped intermittently ("sometimes the jump doesn't register").
The loop latches every frame's edges (after polling, before the steps) into an
`input::EdgeLatch` exposed as `ctx.sim_input`.

- **Rule: `Take()` the edge from `ctx.sim_input`** where you build the immutable
  `LatchedInput` — never read `JustPressed` inside the step, never re-implement the
  latch. `Take()` returns true exactly once per press, across a 0-step or N-step
  frame. Level/held state (`IsDown`) is safe to read live. `Clear()` the latch on
  state transitions so a press made elsewhere can't fire in the new state. A short
  jump buffer (≤1-frame landing window) lives in the feature, orthogonal to the latch.
- Worked examples: `examples/platformer.cpp` and Coin Rush's `LiveInputSource::Next`.

## Content is data, not code

Levels/items/tuning live in **`content/` as declarative files**, validated by a
**CI-runnable pass that fails on dangling references** (bad tile, missing
spawn/exit, unknown id). Systems consume **typed IDs** (`content::levels::kFoo` via
`gen_content`, extending the `gen_assets` idea) and ask the `content` API for the
validated in-memory definition — **systems never parse files**.

## Assets & other kept conventions

- **Typed generated assets.** `tools/gen_assets.py` (CMake configure) scans
  `assets/` → `generated/assets.hpp`; load via `assets::textures::kFoo`, never a
  path string. `view` owns assets and hands `runtime` opaque handles — `runtime`
  never links `resources`.
- **C++23 + engine conventions** (root `AGENTS.md`): `Result<T>`/no-throw, `.clangd`
  Google naming, why-not-how comments, value semantics. Game code is `namespace
  game`; TUs do `using namespace aether;` (contained per game).
- **The opening splash is config, not code.** `assets/config.json`'s `splash`
  block: `enabled`, `preset` (`static` | `cinematic` | `wireframe`), `seconds`,
  `image`, `wire_image`. `ui-studio brand` writes each game its own `splash.png`
  + `splash-wire.png` (a game owns its authored assets), and a missing image
  degrades to no logo rather than to a failure. Two things not to be surprised
  by: it is **forced off** for any capturing, deterministic or frame-bounded run,
  because it presents OUTSIDE the frame loop and every oracle keys off frame
  indices — so `--frames N` gives you no splash by design; and `dev.splash_capture=<prefix>`
  is the only way to see it, since `--capture` implies a measuring run. A game
  wanting its own opening builds one from `app::SplashTimeline` (ADR-0138) —
  `examples/splash_showcase` is the worked example.
- **Headless capture harness — the acceptance oracle.** NAMED options via
  `app::CommandLine` (`--width`, `--height`, `--frames`, `--capture`,
  `--capture-start`, `--capture-frames`, `--level`, + `--record`/`--replay`) — the
  positional `argv[1..7]` contract is gone and the old form is rejected; when a screenshot
  path is set, `ctx.Capturing()` boots straight into gameplay via the autopilot
  script and hides dev UI. After any change: `pixi run build && pixi run test`, then
  capture and eyeball, and (once replay exists) run the replay regression.

## Adding a new game — checklist

1. `mkdir games/<name>/{app,features,runtime,view,content,assets,shaders,tests}`;
   add `add_subdirectory(<name>)` to `games/CMakeLists.txt`.
2. Copy `coin_rush`'s per-layer `CMakeLists.txt`; rename targets + asset namespace;
   keep the graph above. Copy needed `shaders/` sources + the shader-compile block
   from `coin_rush/CMakeLists.txt` (own output dir + `<name>_shaders` target); point
   `AETHER_SHADER_DIR` at it. Never depend on `sandbox_shaders`.
3. Stand up `runtime` (GameWorld + event bus + snapshot + GameApi + seeded RNG +
   replay) and `app` (composition root + input pipeline) first.
   `hearthfield` STARTED sim-only — `runtime` and nothing else, no `app/`, no
   `view/`, no executable — because its first milestone was a headless test
   before anything rendered ([H0](../docs/plans/2026-08-23-hearthfield-h0-skeleton.md)
   §3e). It grew the rest at H1 and is now a complete game, so read that as a
   staging order rather than a standing exception: the shape holds only while
   nothing needs to read the clock, resolve a pick or draw, and the FIRST of
   those three (H2's save) is what ended it.
4. Add gameplay as `features/<x>/` slices (template above) — one per system.
5. Presentation in `view/`, data in `content/`, per-feature tests in
   `features/<x>/tests/` under `<name>_tests` (CTest). Copy
   `tests/label_tests.cmake` too, and the `TEST_INCLUDE_FILES` line that
   includes it: doctest registers its cases at BUILD time, so labelling can
   only happen at CTest time, and a case with NO label is in no set — `ctest -L
   headless` skips it in silence. Three of the four games were unlabelled from
   the split until 2026-09-04 for exactly this reason, which cost that set 178
   of its cases.
6. Per-phase done = builds + tests pass + capture shows the intended state + a
   CMake/deps check (graph acyclic, sim links no `aether::render`, no engine edits).

---

## This is now its own repository (ADR-0170)

`coin_rush`, `lantern`, `tideworn` and `hearthfield` left `aether/games/` on
2026-09-04. The engine is developed in `aether`; these are its consumers.

**Building.** The engine is a SOURCE dependency, not a package — aether installs
and exports nothing, and these games link `aether::*` targets and use its test
helpers. `CMakeLists.txt` finds it via `AETHER_DIR` or the sibling `../aether`,
and `add_subdirectory` does the rest. Everything runs through this repo's own
`pixi.toml` (added 2026-09-04; `pixi task list` is the menu):

```
pixi run configure           # Debug; also Release / Asan / Tsan / DevOff
pixi run build               # extra args reach `cmake --build`
pixi run test                # engine + games; `test-game lantern` for one
pixi run test-headless       # the 1544 cases that need no display
pixi run coin_rush           # build, then play it (--frames/--capture work)
pixi run debug hearthfield   # the same binary under gdb
```

WHAT THAT MANIFEST IS AND IS NOT. It owns `python` and nothing else: every task
delegates into the ENGINE's pixi environment, which is where conan, the pinned
cmake/ninja and the prefix `find_package` searches live — `tools/dev.py` runs
aether's own `configure.py --source .` and `build.py` there, with
`AETHER_BUILD_DIR` pointed at OUR tree. Do not give this manifest build
dependencies of its own: a second set of pins is a second solve, and a games
tree compiled by a different toolchain than the aether it links is the ADR-0171
crash below from the other direction.

`configure` drops the `--keep-current` the older recipe here used, because the
`_build/current` symlink it moves is this repo's own (configure.py writes it
under whatever `--source` it was given), and every task follows it instead of
naming `_build/clang/Debug` by hand.

`run` and `debug` deliberately do NOT enter that environment: the binaries carry
an absolute RPATH into the engine's prefix and bake their asset/shader roots in
as absolute paths, so they run from anywhere with nothing activated.

**What came with the games.** `tools/model_contract.json` (the nine authored
models; aether keeps one fixture for the CONVENTION) and
`asset_rules.moved.json` (the 52 rules that regenerate these assets — the tool
studios stayed in the engine, so running them needs an aether checkout).

**What did not come — RESTORED 2026-09-11.** Six capture oracles
(`coin_rush-win`, `lantern`, `lantern-weather`, three `tideworn-*`), three
`hearthfield-*` cost budgets and two CMake gates (`game_layer_layout`;
`game_sim_layers_link_scene_core`, the half that actually caught something, since
the sim layers named `aether::scene` for a month while a comment claimed
otherwise) were deleted from aether rather than pointed across the boundary. For
that month the whole suite could pass while a game rendered anything at all.

They live in `tools/capture_rules.json` and `tests/CMakeLists.txt` now, recovered
from the engine's history rather than rewritten — so **the digests are the
ORIGINAL ones, and every one still passes.** That makes the gap measured instead
of papered over: nothing in the engine moved a game's picture while nobody was
looking. `pixi run captures-check` runs them, through the engine's own checker
via `--root`. Do not copy that script here; one checker, two repos.

**A CRASH THIS SPLIT CAUSED, AND ITS FIX (ADR-0171).** Six hearthfield UI tests
crashed with SIGFPE inside libstdc++ on the first build here, and the cause is
worth knowing before you add a target: `AETHER_DEV_TOOLS` decides the LAYOUT of
`RenderFrame` (it guards real members), and aether applied it with
`add_compile_definitions()` — a DIRECTORY property, which reached everything
while the games lived under the engine root and reached nothing once they were
a sibling tree. `hearthfield_tests` built a smaller `RenderFrame` than the
`aether_ui` it linked. It is now PUBLIC on `aether_core`, so linking carries
it. If you ever see a crash inside the standard library on data your code did
not touch, diff the two objects' compile commands (`ninja -t commands`) before
reading any code.
