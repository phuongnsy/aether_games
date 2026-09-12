# Infiltration — the vertical slice, promoted from an engine example to the
# repository that owns games (ADR-0170/0172). ONE app layer today: i2 of
# docs/plans/2026-09-11-infiltration-game.md splits it into runtime/features/
# view under the dependency law, and the move stays a relocation so its four
# capture oracles can prove nothing changed.
#
# No shaders/ here, as with lantern: the 3D forward path is engine-owned and
# reached through aether::app's render pipeline. It DOES own its assets — the
# two models and the HUD font are duplicated from the engine's sandbox tree on
# purpose, so that a downstream game owning its render assets stays true.
# ONE include root for the whole game, mirroring the engine's single
# ${AETHER_INCLUDE_DIR}: public headers live at infiltration/include/.
set(INFILTRATION_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(content)
add_subdirectory(runtime)

add_executable(infiltration app/infiltration.cpp)
set_target_properties(infiltration PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                              "${CMAKE_BINARY_DIR}/bin/games")
# aether::inspector as well as app: the slice draws a control panel, which the
# engine keeps out of app so a shipping game links no dev UI it never opens.
target_link_libraries(
  infiltration PRIVATE aether_warnings aether::app aether::inspector
                       infiltration::content infiltration::runtime)
target_include_directories(infiltration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_compile_definitions(
  infiltration
  PRIVATE AETHER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
          AETHER_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
