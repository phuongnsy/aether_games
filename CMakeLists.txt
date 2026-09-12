# The games built on aether, in their own repository (ADR-0170).
#
# HOW THIS FINDS THE ENGINE, and why it is a source dependency rather than a
# package: aether installs and exports nothing. Every game links `aether::*`
# targets and uses the engine's own test helpers (`aether_add_shell_test`,
# `aether_discover_tests`), so what it needs is the engine's CMake tree, not a
# binary. `add_subdirectory` on it is therefore the whole mechanism -- the
# engine builds first, its targets and its helpers come into scope, and the
# games are ordinary downstream consumers exactly as they were when they lived
# inside it.
#
# The games are otherwise SELF-CONTAINED and were before the move: each owns its
# assets, its generated catalog and its shader set, duplicated from the sandbox
# on purpose so that "a downstream game owns its render assets" is true rather
# than aspirational. That is what made this move a relocation and not a port.
cmake_minimum_required(VERSION 3.24)
project(aether_games LANGUAGES CXX)

# AT THE TOP LEVEL, and it has to be here rather than only in the engine: ctest
# descends from the directory where `enable_testing()` was called, so with the
# engine nested the whole tree built its test binaries and registered ZERO
# tests. A suite that builds and reports "No tests were found" is the quietest
# failure in this repo.
enable_testing()

# `AETHER_DIR` first (a checkout anywhere), then the sibling every layout here
# uses. Named rather than searched: a build that goes looking for an engine
# depends on a directory layout nobody declared.
set(AETHER_DIR "$ENV{AETHER_DIR}" CACHE PATH "the aether engine checkout")
if(NOT AETHER_DIR)
  set(AETHER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../aether")
endif()
get_filename_component(AETHER_DIR "${AETHER_DIR}" ABSOLUTE)
if(NOT EXISTS "${AETHER_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "no aether engine at ${AETHER_DIR}. Clone it beside this repo, or "
    "configure with -DAETHER_DIR=<path> / AETHER_DIR=<path> in the "
    "environment.")
endif()

add_subdirectory("${AETHER_DIR}" aether)

add_subdirectory(coin_rush)
add_subdirectory(infiltration)
add_subdirectory(lantern)
add_subdirectory(tideworn)
# hearthfield is SIM-ONLY at H0 -- no app/, no view/, no assets. It is the one
# game here with no executable, deliberately
# (aether/docs/plans/2026-08-23-hearthfield-h0-skeleton.md 3e).
add_subdirectory(hearthfield)

# LAST: both gates in here glob the game directories, and one refuses at
# configure time when its glob collapses, so it must see the games declared.
add_subdirectory(tests)
