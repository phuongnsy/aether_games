# tideworn_tests — the sim's unit suites. HEADLESS by construction: no device
# is created and no render library is linked, which is also the seam this
# proves (games/CLAUDE.md, dependency law 4).
find_package(doctest CONFIG REQUIRED)

add_executable(tideworn_tests main.cpp school_test.cpp voyage_test.cpp)
set_target_properties(tideworn_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                "${CMAKE_BINARY_DIR}/bin/tests")
target_link_libraries(
  tideworn_tests
  PRIVATE aether_warnings
          doctest::doctest
          tideworn::runtime
          tideworn::feat_voyage
          tideworn::feat_school
          tideworn::content)
aether_discover_tests(tideworn_tests)
# The suite's cases join the `headless` label set at CTest time — see
# label_tests.cmake for why it cannot happen here.
set_property(
  DIRECTORY
  APPEND
  PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/label_tests.cmake")
