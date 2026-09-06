# coin_rush progression feature — the round rules (timer, coin tally, exit
# unlock, win). A SIM feature: runtime + core only; reads coins' events + shared
# world state, never another feature's headers.
add_library(cr_feat_progression STATIC src/system.cpp)
add_library(coin_rush::feat_progression ALIAS cr_feat_progression)

target_include_directories(cr_feat_progression PUBLIC ${COIN_RUSH_INCLUDE_DIR})
target_link_libraries(
  cr_feat_progression
  PUBLIC coin_rush::runtime aether::core
  PRIVATE aether_warnings)
