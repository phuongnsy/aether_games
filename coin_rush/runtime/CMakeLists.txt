# coin_rush runtime — the game's deterministic fixed-step foundation: GameWorld,
# the event bus, the ViewSnapshot contract, GameApi, the seeded RNG, replay. This
# is a SIM layer: it links NO aether::render and creates no device (see
# games/CLAUDE.md, "the dependency law"). It links aether::scene_core for the world /
# collision model, which transitively pulls rhi/bgfx today — the render-free split
# is docs/plans/2026-07-21-scene-collision-core-split.md.
add_library(cr_runtime STATIC src/game_world.cpp)
add_library(coin_rush::runtime ALIAS cr_runtime)

target_include_directories(cr_runtime PUBLIC ${COIN_RUSH_INCLUDE_DIR})
target_link_libraries(
  cr_runtime
  PUBLIC aether::core
         aether::scene_core
         aether::worldsim
         aether::surface
         aether::weather
  PRIVATE aether_warnings)
