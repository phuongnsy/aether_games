# infiltration view — what the slice looks like. THE ONLY LAYER that may name
# aether::scene or aether::render; the sim layers are greped for those names
# and refused. Today it draws debug geometry and needs neither, which is why
# it links what a producer of RenderFrames links and nothing more.
add_library(infiltration_view STATIC src/draw.cpp)
add_library(infiltration::view ALIAS infiltration_view)
target_include_directories(infiltration_view PUBLIC ${INFILTRATION_INCLUDE_DIR})
target_link_libraries(
  infiltration_view
  PUBLIC aether::core aether::anim aether::resources aether::ai aether::nav aether::physics aether::scene_core infiltration::content infiltration::feat_guards infiltration::feat_ragdoll infiltration::runtime
  PRIVATE aether_warnings)
