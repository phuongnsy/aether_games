# lantern weather feature — 3D rain/snow over the spire (weather-3d t4). A SIM
# feature: fields + impacts + surface response, no render, no device. The view
# Emits its fields; app registers the platform patches.
add_library(lantern_feat_weather STATIC src/weather.cpp)
add_library(lantern::feat_weather ALIAS lantern_feat_weather)
target_include_directories(lantern_feat_weather PUBLIC ${LANTERN_INCLUDE_DIR})
target_link_libraries(
  lantern_feat_weather
  PUBLIC aether::core
         aether::scene_core
         aether::weather
         aether::worldsim
         aether::surface
         lantern::content
  PRIVATE aether_warnings)
