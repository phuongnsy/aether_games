# hearthfield production feature. A SIM feature: pure step, no render, no device.
add_library(hf_feat_production STATIC src/system.cpp)
add_library(hf::feat_production ALIAS hf_feat_production)
target_include_directories(hf_feat_production PUBLIC ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(
  hf_feat_production
  PUBLIC hf::runtime aether::core
  PRIVATE aether_warnings)
