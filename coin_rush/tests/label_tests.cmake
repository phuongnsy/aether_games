# CTest-time labelling, and it has to be CTest-time: doctest registers its
# cases when the binary is BUILT, so at configure time there is no test to set
# a property on (the engine's cmake/label_tests.cmake.in says the same for its
# own suites; the pointer here used to name a tests/label_tests.cmake that
# does not exist). Every coin_rush case is headless by design — the sim
# layer creates no device (AGENTS.md, the dependency law) — so the whole
# suite joins the default `headless` set with no exceptions to enumerate.
foreach(coin_rush_test IN LISTS coin_rush_tests_TESTS)
  set_tests_properties("${coin_rush_test}" PROPERTIES LABELS headless)
endforeach()
