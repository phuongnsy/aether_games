# infiltration content — the level geometry and the level as a query.
# core only: it describes a space, and knows nothing that draws one.
add_library(infiltration_content STATIC src/level.cpp)
add_library(infiltration::content ALIAS infiltration_content)
target_include_directories(infiltration_content PUBLIC ${INFILTRATION_INCLUDE_DIR})
target_link_libraries(infiltration_content PUBLIC aether::core PRIVATE aether_warnings)
