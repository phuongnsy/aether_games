# coin_rush view — ALL presentation: the HUD (from a data snapshot), the vfx
# runtime glue, the post/lighting/bloom render pipeline (Renderer), and the
# per-object materials (SceneMaterials). Depends on runtime (the snapshot/event
# vocabulary) + the engine's presentation libs; NEVER on a feature (the
# dependency law — view reacts to events, it doesn't reach into gameplay).
add_library(cr_view STATIC src/hud.cpp src/effects.cpp src/renderer.cpp
                           src/scene_materials.cpp)
add_library(coin_rush::view ALIAS cr_view)

target_include_directories(
  cr_view
  PUBLIC ${COIN_RUSH_INCLUDE_DIR}
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../generated"
)# gen_assets typed constants
target_link_libraries(
  cr_view
  PUBLIC coin_rush::runtime
         aether::core
         aether::app
         aether::ui
         aether::scene
         aether::render
         aether::rhi
         aether::platform
         aether::resources
         aether::effects
  PRIVATE aether_warnings)
