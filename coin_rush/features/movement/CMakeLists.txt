# coin_rush movement feature — the kinematic character controller. A SIM feature:
# depends on the runtime foundation + core only; talks to other features via
# events + shared component data (never their headers). Pure logic → unit-tested
# headless (see tests/, wired into coin_rush_tests).
add_library(cr_feat_movement STATIC src/system.cpp)
add_library(coin_rush::feat_movement ALIAS cr_feat_movement)

target_include_directories(cr_feat_movement PUBLIC ${COIN_RUSH_INCLUDE_DIR})
target_link_libraries(
  cr_feat_movement
  PUBLIC coin_rush::runtime aether::core
  PRIVATE aether_warnings)
