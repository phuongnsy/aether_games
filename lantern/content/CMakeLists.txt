# lantern content — declarative data only (the spire's paths and tuning). Header
# only today; the library exists so the layer boundary is enforced by CMake from
# day one rather than retrofitted (games/CLAUDE.md).
add_library(lantern_content INTERFACE)
add_library(lantern::content ALIAS lantern_content)
target_include_directories(lantern_content INTERFACE ${LANTERN_INCLUDE_DIR})
target_link_libraries(lantern_content INTERFACE aether::core)
