# infiltration ragdoll — the bodies a downed guard becomes.
# A SIM layer: greped by game_sim_layers_link_scene_core, and physics links
# core alone, so the rule holds by construction rather than by comment.
add_library(infiltration_ragdoll STATIC src/ragdoll.cpp)
add_library(infiltration::feat_ragdoll ALIAS infiltration_ragdoll)
target_include_directories(infiltration_ragdoll PUBLIC ${INFILTRATION_INCLUDE_DIR})
target_link_libraries(
  infiltration_ragdoll
  PUBLIC aether::core aether::physics
  PRIVATE aether_warnings)
