# ONE include root for the whole game, mirroring the engine's single
# ${AETHER_INCLUDE_DIR}: public headers live at games/hearthfield/include/,
# private ones stay in each layer's src/. A layer's PUBLIC surface is a
# property of the game, not of the directory it happens to be built from.
set(HEARTHFIELD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Hearthfield — the farming target
# (docs/specs/2026-08-23-hearthfield-game-target.md).
#
# Like lantern and tideworn, no shaders/ here: the 3D forward path is
# engine-owned and reached through aether::app's HdrPipeline. It DOES own its
# assets — the board's two primitives and the farm world chunk under assets/,
# with rules in tools/asset_rules.json.
add_subdirectory(content) # the crop catalogue (declarative data only)
add_subdirectory(runtime) # sim foundation (tick, GameWorld, bus, snapshot)
# The five slices, in the REGISTRATION order app/ uses (spec §5.1): a crop
# harvested this step can feed a building this step, and that building's output
# can fill an order next step. Livestock is H5.
add_subdirectory(features/plots) # sow / grow / harvest
add_subdirectory(features/production) # the mill: committed queue, deadline loop
add_subdirectory(features/livestock
)# the coop: the trough commits, like the mill
add_subdirectory(features/orders) # the board, drawn from the world RNG
add_subdirectory(features/economy) # coin, the barn cap, land purchase
add_subdirectory(features/placement
)# where a building may stand, and what it costs
add_subdirectory(view) # presentation: the board's scene nodes
add_subdirectory(tests) # hearthfield_tests (headless, CTest)

set(_hf_app_sources app/hearthfield.cpp app/screens.cpp)
add_executable(hearthfield app/main.cpp ${_hf_app_sources})
set_target_properties(hearthfield PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                             "${CMAKE_BINARY_DIR}/bin/games")
target_link_libraries(
  hearthfield
  PRIVATE aether_warnings
          aether::app
          hf::runtime
          hf::content
          hf::view
          hf::feat_plots
          hf::feat_production
          hf::feat_livestock
          hf::feat_orders
          hf::feat_economy
          hf::feat_placement
          aether::ui)
target_include_directories(hearthfield
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_compile_definitions(
  hearthfield PRIVATE AETHER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# Android needs a SHARED LIBRARY, because NativeActivity dlopen()s the game
# rather than exec()ing it (coin_rush's precedent, same shape). The desktop
# executable stays alongside: the two differ by ONE source file, the entry
# point, and everything else is shared.
#
# This target is also the ONLY place the board camera's pinch and twist ever
# meet a real finger — glfw reports no contacts at all, so every other platform
# exercises the mouse path and leaves the gesture recognizer untested.
if(ANDROID)
  add_library(hearthfield_apk SHARED app/main_android.cpp ${_hf_app_sources})
  # libhearthfield.so is the name the manifest's android.app.lib_name refers
  # to, so the target name must not leak into the artifact.
  set_target_properties(
    hearthfield_apk
    PROPERTIES OUTPUT_NAME hearthfield
               LIBRARY_OUTPUT_DIRECTORY
               "${CMAKE_BINARY_DIR}/bin/android/${ANDROID_ABI}")
  target_link_libraries(
    hearthfield_apk
    PRIVATE aether_warnings
            aether::app
            aether::android_glue
            hf::runtime
            hf::content
            hf::view
            hf::feat_plots
            hf::feat_production
            hf::feat_livestock
            hf::feat_orders
            hf::feat_economy
            hf::feat_placement
            aether::ui)
  target_include_directories(hearthfield_apk
                             PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app")
  target_compile_definitions(
    hearthfield_apk
    PRIVATE AETHER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
