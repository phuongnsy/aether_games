# games/coin_rush — the worked reference game

Root: [`AGENTS.md`](../../AGENTS.md) · **read [`AGENTS.md`](../AGENTS.md) first** —
it defines the layered feature-slice architecture every rule below instantiates.

Coin Rush is the **worked example** of `AGENTS.md`: a weather platformer built
as layered libraries. When you're unsure how a layer, target, event, or the replay
path should look, read it here.

## Layout (each a static lib; the dependency law is enforced in CMake)

- `app/` — composition root: `main`, the `CoinRushGame` orchestrator, flow screens,
  the input pipeline (`InputSource`: live/autopilot/replay). Sees everything.
- `runtime/` — `GameWorld` + the `SimSystem` bus, event bus (`GameEvent`), `WorldView`
  (shared sim state), `ViewSnapshot`, `GameApi`, seeded RNG, replay recorder.
- `features/` — one slice per system: `movement`, `coins`, `progression`, `weather`
  (each `components`/`system`/`events`/`tuning`/`tests`). Talk via events + shared
  world data only.
- `view/` — presentation: `Renderer` (post/lighting/bloom pipeline), `SceneMaterials`
  (per-object shaders), `hud`, effects glue.
- `content/` — declarative `.level` files → `gen_content` → typed IDs.
- `assets/` + `shaders/` — the game's OWN art/audio/fonts + shader set (its own
  compile; never borrows the sandbox).
- `tests/` — `coin_rush_tests` (per-feature unit + headless-sim + replay-determinism).

## Working here

- **This game is C++ ONLY, and that is a decision rather than a gap** (ADR-0152).
  Four of its features carried a parallel Lua backend from 2026-08-31 to
  2026-09-01 — `--scripted`, `bin2c`-embedded `.lua`, per-feature parity A/Bs,
  script hot-reload — and all of it is gone. It existed to answer whether the
  layered structure was a real seam; it was, and the answer is written down
  (ADR-0150/0151, `docs/perf/script-seam-baseline.md`). What replaced it is
  [`games/lua/coin_rush`](../lua/AGENTS.md): the same game as one Lua file, run
  by `aether_sim`. Keeping both would have made three implementations of one
  rule set with a consumer for one. **Do not re-add a script backend here** —
  the scripted version of this game already exists somewhere else.
- **Sim is headless + deterministic:** `GameWorld.Step(LatchedInput, dt)` is a pure
  function (no wall clock, one seeded RNG). The **byte-identical capture** (record a
  run → replay → `cmp`) is the standing regression oracle: after any change, the win
  capture must come back pixel-identical. **Enforced** by `pixi run captures-check`
  (`coin_rush-win` in `tools/capture_rules.json`) — it is the only oracle in that
  set that tests the SIM rather than pixels, being one frame 900 fixed steps deep.
- Verify: `pixi run build && pixi run test`, then
  `DISPLAY=:0 _build/current/bin/games/coin_rush --width 1280 --height 720
  --frames <n> --capture out --capture-start <start> --capture-frames <count>
  --level <level>` → **YOU WIN 8/8**. NAMED options, not positional: the
  `argv[1..7]` contract is gone (`app::CommandLine`), and the old form is now
  rejected outright.

## Ask before

- **Anything that needs an engine change** — do NOT hack it into game code; file an
  engine plan under `docs/plans/` and leave a TODO (the render-free scene-core split is
  the precedent).
- **Breaking a layer boundary** — the dependency law is CMake-enforced + seam-tested;
  a violation is a compile/link error by design.

## See also

[`AGENTS.md`](../AGENTS.md) (the architecture); the migration plan
`.claude/plans/` + `docs/plans/2026-07-21-coin-rush-view-renderer-extraction.md`.
