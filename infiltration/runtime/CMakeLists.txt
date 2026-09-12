# infiltration runtime — the deterministic half: what the player is, and (in
# the cuts after this one) how a fixed step moves it.
#
# A SIM layer, which is a rule the build enforces rather than a comment: the
# games' `game_sim_layers_link_scene_core` test greps this file and fails if it
# ever names aether::scene or aether::render.
add_library(infiltration_runtime STATIC src/player.cpp)
add_library(infiltration::runtime ALIAS infiltration_runtime)
target_include_directories(infiltration_runtime PUBLIC ${INFILTRATION_INCLUDE_DIR})
target_link_libraries(
  infiltration_runtime
  PUBLIC aether::core aether::anim aether::nav infiltration::content
  PRIVATE aether_warnings)
