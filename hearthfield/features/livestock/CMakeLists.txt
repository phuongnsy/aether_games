# hearthfield livestock feature. A SIM feature: pure step, no render, no device.
add_library(hf_feat_livestock STATIC src/system.cpp)
add_library(hf::feat_livestock ALIAS hf_feat_livestock)
target_include_directories(hf_feat_livestock PUBLIC ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(
  hf_feat_livestock
  PUBLIC hf::runtime aether::core
  PRIVATE aether_warnings)
