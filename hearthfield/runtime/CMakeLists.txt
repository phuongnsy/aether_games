# hearthfield runtime — the deterministic fixed step: the farm clock, GameWorld,
# the event bus, the shared plot table and the ViewSnapshot contract. A SIM
# layer: NO aether::render, no device (AGENTS.md dependency law 4).
#
# It does not link aether::scene_core yet either, because H0's sim is plain
# data with no spatial query in it. That arrives with picking at H1.
add_library(hf_runtime STATIC src/game_world.cpp)
add_library(hf::runtime ALIAS hf_runtime)
target_include_directories(hf_runtime PUBLIC ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(
  hf_runtime
  PUBLIC aether::core hf::content
  PRIVATE aether_warnings)
