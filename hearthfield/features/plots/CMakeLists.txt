# hearthfield plots feature — grid, sowing, growth, harvest. Owns ready_tick
# and the closed-form offline advance. A SIM feature: pure step, no render, no
# device.
add_library(hf_feat_plots STATIC src/system.cpp)
add_library(hf::feat_plots ALIAS hf_feat_plots)
target_include_directories(hf_feat_plots PUBLIC ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(
  hf_feat_plots
  PUBLIC hf::runtime aether::core
  PRIVATE aether_warnings)
