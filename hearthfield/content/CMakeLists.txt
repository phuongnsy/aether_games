# hearthfield content — declarative data only: the crop catalogue, validated by
# a constexpr pass rather than at runtime. Header only today; the library exists
# so the layer boundary is enforced by CMake from day one rather than
# retrofitted (games/CLAUDE.md).
add_library(hf_content INTERFACE)
add_library(hf::content ALIAS hf_content)
target_include_directories(hf_content INTERFACE ${HEARTHFIELD_INCLUDE_DIR})
target_link_libraries(hf_content INTERFACE aether::core)
