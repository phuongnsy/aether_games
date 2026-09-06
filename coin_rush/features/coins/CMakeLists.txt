# coin_rush coins feature — coin pickup detection. A SIM feature: depends on the
# runtime foundation + core only; talks to the movement feature only through
# shared world state (the player position) + step ordering, never its headers.
add_library(cr_feat_coins STATIC src/system.cpp)
add_library(coin_rush::feat_coins ALIAS cr_feat_coins)

target_include_directories(cr_feat_coins PUBLIC ${COIN_RUSH_INCLUDE_DIR})
target_link_libraries(
  cr_feat_coins
  PUBLIC coin_rush::runtime aether::core
  PRIVATE aether_warnings)
