# coin_rush content — the declarative level set. gen_content.py VALIDATES the
# *.level files at configure time (a ragged map / missing spawn-exit-coin fails
# the build — the dangling-reference guard) and emits the level data + typed IDs
# into generated/content_levels.hpp. cr_content is the pure (core-only) API over
# it; systems consume levels by index/typed-id, never by parsing files.
set(_content_hpp "${CMAKE_CURRENT_SOURCE_DIR}/../generated/content_levels.hpp")
execute_process(
  COMMAND "${AETHER_PYTHON}" "${AETHER_ROOT}/tools/gen_content.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/levels" "${_content_hpp}"
  RESULT_VARIABLE _gen_content_rc)
if(NOT _gen_content_rc EQUAL 0)
  message(FATAL_ERROR "gen_content.py failed (${_gen_content_rc})")
endif()

add_library(cr_content STATIC src/levels.cpp)
add_library(coin_rush::content ALIAS cr_content)
target_include_directories(
  cr_content
  PUBLIC ${COIN_RUSH_INCLUDE_DIR}
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../generated")
target_link_libraries(
  cr_content
  PUBLIC aether::core
  PRIVATE aether_warnings)
