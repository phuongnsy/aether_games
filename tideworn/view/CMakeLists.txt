# tideworn view — presentation only. Consumes the runtime's snapshot; depends
# on NO feature (AGENTS.md law 2). This is the layer allowed to link
# aether::render.
add_library(
  tideworn_view STATIC
  src/creatures.cpp
  src/disturbance.cpp
  src/rain.cpp
  src/sea.cpp
  src/sky_ring.cpp
  src/spray.cpp)
add_library(tideworn::view ALIAS tideworn_view)
target_include_directories(tideworn_view PUBLIC ${TIDEWORN_INCLUDE_DIR})
target_link_libraries(
  tideworn_view
  PUBLIC tideworn::runtime aether::render aether::scene aether::weather
  PRIVATE aether_warnings)
