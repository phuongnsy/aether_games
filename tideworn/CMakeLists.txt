# ONE include root for the whole game, mirroring the engine's single
# ${AETHER_INCLUDE_DIR}: public headers live at games/tideworn/include/,
# private ones stay in each layer's src/. A layer's PUBLIC surface is a
# property of the game, not of the directory it happens to be built from.
set(TIDEWORN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Tideworn — the open-sea game (docs/plans/2026-08-16-open-sea-game.md).
#
# Like lantern, no shaders/ here: the 3D forward path and the water are
# engine-owned and reached through aether::app's render pipeline. It DOES own
# its assets — the ocean environment and the hull under assets/, with rules in
# tools/asset_rules.json.
add_subdirectory(content)
add_subdirectory(features/voyage)
add_subdirectory(features/school)
add_subdirectory(runtime)
add_subdirectory(view)

add_executable(tideworn app/main.cpp app/tideworn.cpp)
set_target_properties(tideworn PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                          "${CMAKE_BINARY_DIR}/bin/games")
target_link_libraries(
  tideworn
  PRIVATE aether_warnings
          aether::app
          tideworn::runtime
          tideworn::content
          tideworn::view
          tideworn::feat_voyage
          tideworn::feat_school)
target_include_directories(tideworn PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_compile_definitions(
  tideworn PRIVATE AETHER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_subdirectory(tests)
