# hearthfield economy feature. A SIM feature: pure step, no render, no device.
add_library(hf_feat_economy STATIC src/system.cpp)
add_library(hf::feat_economy ALIAS hf_feat_economy)
target_include_directories(hf_feat_economy PUBLIC ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(
  hf_feat_economy
  PUBLIC hf::runtime aether::core
  PRIVATE aether_warnings)
