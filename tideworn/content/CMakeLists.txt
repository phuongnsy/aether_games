# tideworn content — declarative data only (asset paths, the voyage schedule).
# Header only today; the library exists so the layer boundary is enforced by
# CMake from day one rather than retrofitted (AGENTS.md).
add_library(tideworn_content INTERFACE)
add_library(tideworn::content ALIAS tideworn_content)
target_include_directories(tideworn_content INTERFACE ${TIDEWORN_INCLUDE_DIR})
target_link_libraries(tideworn_content INTERFACE aether::core)
