# tideworn voyage feature — the sea-state schedule: voyage clock, wind, day
# fraction, weather bands. A SIM feature: pure step, no render, no device.
add_library(tideworn_feat_voyage STATIC src/system.cpp)
add_library(tideworn::feat_voyage ALIAS tideworn_feat_voyage)
target_include_directories(tideworn_feat_voyage PUBLIC ${TIDEWORN_INCLUDE_DIR})
target_link_libraries(
  tideworn_feat_voyage
  PUBLIC aether::core tideworn::content
  PRIVATE aether_warnings)
