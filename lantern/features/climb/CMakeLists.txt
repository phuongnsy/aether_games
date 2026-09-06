# lantern climb feature — the kinematic character controller, graduated from
# examples/lab/controller_lab. A SIM feature: runtime + core only, no render,
# no device. Its behaviour is pinned by tests/ (headless, deterministic).
add_library(lantern_feat_climb STATIC src/walker.cpp)
add_library(lantern::feat_climb ALIAS lantern_feat_climb)
target_include_directories(lantern_feat_climb PUBLIC ${LANTERN_INCLUDE_DIR})
target_link_libraries(
  lantern_feat_climb
  PUBLIC aether::core aether::scene_core
  PRIVATE aether_warnings)
