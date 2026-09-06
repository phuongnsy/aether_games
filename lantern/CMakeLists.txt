# ONE include root for the whole game, mirroring the engine's single
# ${AETHER_INCLUDE_DIR}: public headers live at games/lantern/include/,
# private ones stay in each layer's src/. A layer's PUBLIC surface is a
# property of the game, not of the directory it happens to be built from.
set(LANTERN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Lantern — the 3D vertical slice (docs/specs/2026-08-07-lantern-game-target.md).
#
# No shaders/ here, unlike coin_rush: the 3D forward path is engine-owned and
# reached through aether::app's render pipeline, so this game authors no .sc
# sources. It DOES own its assets — the spire world, the character and the HUD
# font all live under assets/ with rules in tools/asset_rules.json.
add_subdirectory(content)
add_subdirectory(features/climb)
add_subdirectory(features/lanterns)
add_subdirectory(features/weather)
add_subdirectory(runtime)
add_subdirectory(view)

add_executable(lantern app/main.cpp app/lantern.cpp)
set_target_properties(lantern PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                         "${CMAKE_BINARY_DIR}/bin/games")
target_link_libraries(
  lantern
  PRIVATE aether_warnings
          aether::app
          lantern::runtime
          lantern::content
          lantern::view
          lantern::feat_climb
          lantern::feat_lanterns
          lantern::feat_weather)
target_include_directories(lantern PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_compile_definitions(
  lantern PRIVATE AETHER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_subdirectory(tests)
