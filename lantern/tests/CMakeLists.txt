# lantern_tests — the sim's unit suites. HEADLESS by construction: no device is
# created and no render library is linked, which is also the seam this proves
# (AGENTS.md, dependency law 4).
find_package(doctest CONFIG REQUIRED)

add_executable(lantern_tests main.cpp lanterns_test.cpp)
set_target_properties(lantern_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                               "${CMAKE_BINARY_DIR}/bin/tests")
target_link_libraries(
  lantern_tests
  PRIVATE aether_warnings
          doctest::doctest
          lantern::runtime
          lantern::feat_lanterns
          lantern::content)
aether_discover_tests(lantern_tests)
# The suite's cases join the `headless` label set at CTest time — see
# label_tests.cmake for why it cannot happen here.
set_property(
  DIRECTORY
  APPEND
  PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/label_tests.cmake")
