# lantern runtime — the deterministic fixed step: GameWorld, the event bus and
# the ViewSnapshot contract. A SIM layer: NO aether::render, no device. It links
# aether::scene_core for the collision model — which since 2026-08-20 no longer
# pulls rhi/bgfx at all, so the strict rule holds by construction rather than by
# convention (docs/plans/2026-07-21-scene-collision-core-split.md)
# (docs/plans/2026-07-21-scene-collision-core-split.md is the render-free split).
add_library(lantern_runtime STATIC src/game_world.cpp)
add_library(lantern::runtime ALIAS lantern_runtime)
target_include_directories(lantern_runtime PUBLIC ${LANTERN_INCLUDE_DIR})
target_link_libraries(
  lantern_runtime
  PUBLIC aether::core
         aether::scene_core
         lantern::content
         lantern::feat_climb
         lantern::feat_lanterns
         lantern::feat_weather
  PRIVATE aether_warnings)
