# tideworn school feature — marine-life flocking: boids + depth band + boat
# anchor. A SIM feature: pure step, no render, no device.
add_library(tideworn_feat_school STATIC src/system.cpp)
add_library(tideworn::feat_school ALIAS tideworn_feat_school)
target_include_directories(tideworn_feat_school PUBLIC ${TIDEWORN_INCLUDE_DIR})
target_link_libraries(
  tideworn_feat_school
  PUBLIC aether::core tideworn::content
  PRIVATE aether_warnings)
