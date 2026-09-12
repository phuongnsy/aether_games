# infiltration guards — perception, policy and the crowd's half of a step.
# A SIM layer: the games' game_sim_layers_link_scene_core test greps this file
# and fails the build if it ever names a render library.
add_library(infiltration_guards STATIC src/guards.cpp)
add_library(infiltration::feat_guards ALIAS infiltration_guards)
target_include_directories(infiltration_guards PUBLIC ${INFILTRATION_INCLUDE_DIR})
target_link_libraries(
  infiltration_guards
  PUBLIC aether::core aether::ai aether::anim aether::nav
  PRIVATE aether_warnings)
