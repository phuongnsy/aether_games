# hearthfield view — ALL presentation: the board's scene nodes, built from the
# ViewSnapshot. Consumes the snapshot and the drained events; never a feature
# (AGENTS.md dependency law 2).
add_library(
  hf_view STATIC
  src/board.cpp
  src/hud.cpp
  src/loading.cpp
  src/weather_fx.cpp
  src/steading.cpp
  src/farm_audio.cpp
  src/camera_gestures.cpp
  src/camera_input.cpp
  src/camera_rig.cpp
  src/buildings.cpp)
add_library(hf::view ALIAS hf_view)
target_include_directories(hf_view PUBLIC ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(
  hf_view
  PUBLIC aether::core
         aether::app
         aether::scene
         aether::resources
         aether::ui
         aether::weather
         aether::surface
         aether::audio
         hf::runtime
         hf::content
  PRIVATE aether_warnings)
