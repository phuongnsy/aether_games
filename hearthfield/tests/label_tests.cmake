# CTest-time labelling, and it has to be CTest-time: doctest registers its
# cases when the binary is BUILT, so at configure time there is no test to set
# a property on. `hearthfield_tests_TESTS` is the list doctest publishes for
# exactly this, reached through the TEST_INCLUDE_FILES directory property — the
# same mechanism the engine's cmake/label_tests.cmake.in uses for its suites.
#
# WHY THIS FILE EXISTS AT ALL, since a label looks like bookkeeping: `-L
# headless` is the set the engine's fast and sanitized runs select, and an
# UNLABELLED case belongs to no set, so those runs skip it in silence. This
# suite was unlabelled until 2026-09-04 — coin_rush was the only game with this
# file — which made `ctest -L headless` here cover one game of four. It is the
# biggest suite in this repo (163 cases), so it was also most of what went
# unrun.
#
# Every hearthfield case is headless, and here that is worth stating rather
# than inheriting: this suite is the one that links `hf::view` and
# `aether::ui`, so it is the one where the claim could plausibly be false. It
# is not — those are linked for LAYOUT, which is data, and no case creates a
# device or a window. Checked rather than asserted: the suite was run on
# 2026-09-04 with DISPLAY and WAYLAND_DISPLAY unset and passed whole, all 163.
foreach(hearthfield_test IN LISTS hearthfield_tests_TESTS)
  set_tests_properties("${hearthfield_test}" PROPERTIES LABELS headless)
endforeach()
