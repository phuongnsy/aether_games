# hearthfield placement feature. A SIM feature: pure step, no render, no device.
add_library(hf_feat_placement STATIC src/system.cpp)
add_library(hf::feat_placement ALIAS hf_feat_placement)
target_include_directories(hf_feat_placement PUBLIC ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(
  hf_feat_placement
  PUBLIC hf::runtime aether::core
  PRIVATE aether_warnings)
