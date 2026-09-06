# hearthfield orders feature. A SIM feature: pure step, no render, no device.
add_library(hf_feat_orders STATIC src/system.cpp)
add_library(hf::feat_orders ALIAS hf_feat_orders)
target_include_directories(hf_feat_orders PUBLIC ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(
  hf_feat_orders
  PUBLIC hf::runtime aether::core
  PRIVATE aether_warnings)
