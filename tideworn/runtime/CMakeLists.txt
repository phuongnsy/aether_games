# tideworn runtime — the deterministic fixed step: GameWorld, the event bus
# and the ViewSnapshot contract. A SIM layer: NO aether::render, no device
# (AGENTS.md dependency law 4).
add_library(tideworn_runtime STATIC src/game_world.cpp)
add_library(tideworn::runtime ALIAS tideworn_runtime)
target_include_directories(tideworn_runtime PUBLIC ${TIDEWORN_INCLUDE_DIR})
target_link_libraries(
  tideworn_runtime
  PUBLIC aether::core tideworn::content tideworn::feat_voyage
         tideworn::feat_school
  PRIVATE aether_warnings)
