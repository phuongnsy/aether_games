# lantern view — ALL presentation: lantern lights, the HUD. Consumes the
# runtime's snapshot and events; depends on NO feature (AGENTS.md law 2).
add_library(lantern_view STATIC src/lights.cpp src/hud.cpp src/weather_fx.cpp)
add_library(lantern::view ALIAS lantern_view)
target_include_directories(lantern_view PUBLIC ${LANTERN_INCLUDE_DIR})
target_link_libraries(
  lantern_view
  PUBLIC lantern::runtime aether::ui aether::resources
  PRIVATE aether_warnings)
