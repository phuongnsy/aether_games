# CTest-time labelling, and it has to be CTest-time: doctest registers its
# cases when the binary is BUILT, so at configure time there is no test to set
# a property on. `tideworn_tests_TESTS` is the list doctest publishes for
# exactly this, reached through the TEST_INCLUDE_FILES directory property — the
# same mechanism the engine's cmake/label_tests.cmake.in uses for its suites.
#
# WHY THIS FILE EXISTS AT ALL, since a label looks like bookkeeping: `-L
# headless` is the set the engine's fast and sanitized runs select, and an
# UNLABELLED case belongs to no set, so those runs skip it in silence. This
# suite was unlabelled until 2026-09-04 — coin_rush was the only game with this
# file — which made `ctest -L headless` here cover one game of four.
#
# Every tideworn case is headless by design: the sim layer creates no device
# and links no render library (AGENTS.md, dependency law 4), so the whole
# suite joins the default `headless` set with no exceptions to enumerate.
# Checked rather than asserted — the suite was run on 2026-09-04 with DISPLAY
# and WAYLAND_DISPLAY unset and passed whole.
foreach(tideworn_test IN LISTS tideworn_tests_TESTS)
  set_tests_properties("${tideworn_test}" PROPERTIES LABELS headless)
endforeach()
