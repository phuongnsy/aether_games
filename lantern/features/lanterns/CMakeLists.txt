# lantern lanterns feature — lit state, proximity and the checkpoint rule. Pure
# data + arithmetic: no scene, no lights, no render. Turning this state into a
# spot light is view's job.
add_library(lantern_feat_lanterns STATIC src/lanterns.cpp)
add_library(lantern::feat_lanterns ALIAS lantern_feat_lanterns)
target_include_directories(lantern_feat_lanterns PUBLIC ${LANTERN_INCLUDE_DIR})
target_link_libraries(
  lantern_feat_lanterns
  PUBLIC aether::core
  PRIVATE aether_warnings)
