# Cross-game structural gates — the two ADR-0172 deleted rather than relocated.
#
# Recovered 2026-09-11 from the engine at 990def0e^:tests/CMakeLists.txt, with
# their reasoning, and re-aimed at this repo's layout: the old scans were rooted
# at `games/<name>/`, and here a game IS a top-level directory. The engine's note
# at tests/CMakeLists.txt:398 records what each one proved and is the reason they
# came back rather than being rewritten from the description.
#
# They need `aether_add_shell_test`, which arrives with the engine subtree.

# Every game directory, derived rather than spelled: a new game must edit the
# top-level CMakeLists.txt to exist at all, which reconfigures and re-globs.
file(GLOB _game_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/.."
     "${CMAKE_CURRENT_SOURCE_DIR}/../*/CMakeLists.txt")
list(TRANSFORM _game_dirs REPLACE "/CMakeLists.txt$" "")
list(REMOVE_ITEM _game_dirs "tests")
list(LENGTH _game_dirs _game_count)
# JOINED, never interpolated as a list. A CMake list expands to `a;b;c`, and the
# shell splits that into three COMMANDS — so `find ${_game_dirs} -name '*.hpp'`
# ran a bare `find coin_rush`, listed the entire tree, and the gate failed for a
# reason that had nothing to do with the layout. Same trap AetherTest.cmake's own
# ARGV comment documents.
list(JOIN _game_dirs "' '" _game_args)
if(_game_count LESS 4)
  message(
    FATAL_ERROR
      "game_layer_layout found ${_game_count} games — the glob no longer "
      "matches this repo's layout, so both gates below would scan nothing. "
      "Fix the pattern in tests/CMakeLists.txt.")
endif()

# A game's layout mirrors the engine's: ONE public include root per game at
# <name>/include/<prefix>/<layer>/, private headers in each layer's src/.
#
# THREE assertions, and the previous version of this test is why they are checked
# by breaking the layout on purpose. It asserted feature paths under
# `games/*/features/*/include`, and a refactor deleted those directories — so the
# glob matched nothing and the assertion could no longer fail. An assertion that
# cannot fail is worse than none, and it was introduced by the very change it was
# meant to police.
#
# `-mindepth 2` here where the engine's copy said 3: one path segment came off
# every scan when `games/` stopped being a directory.
aether_add_shell_test(
  game_layer_layout
  "cd '${CMAKE_CURRENT_SOURCE_DIR}/..' && \
     ! find '${_game_args}' -mindepth 2 -type d -name include | grep . && \
     ! find '${_game_args}' -name '*.hpp' | grep -vE '^[^/]+/include/' | grep -vE '/(app|src|generated|tests)/' | grep . && \
     ! find '${_game_args}' -path '*/include/*' -name '*.hpp' | sed 's|.*/include/||' | grep -vE '^[^/]+/[^/]+/' | grep ."
)

# A game's SIM layer links scene_core, never scene — which is what makes "sim
# links no render libraries" true by construction instead of by convention.
#
# `scene_core_deps_downward` in the engine already proves the LIBRARY carries no
# render dependency. This proves the CONSUMERS actually use it: the sim layers
# named aether::scene for a month while a comment claimed they were headless,
# because scene pulled resources -> rhi -> bgfx transitively and nothing checked.
# A grep, because a link interface has no seam a unit test can reach. This is the
# half that caught something.
#
# `^[^#]*` so a COMMENT saying "links NO aether::render" is not a match — the
# first version failed on exactly those two comments, a useful reminder that a
# grep test asserts text and not meaning.
file(GLOB _sim_cmake "${CMAKE_CURRENT_SOURCE_DIR}/../*/runtime/CMakeLists.txt"
     "${CMAKE_CURRENT_SOURCE_DIR}/../*/features/*/CMakeLists.txt")
list(LENGTH _sim_cmake _sim_count)
# A glob that collapses is the failure this file's whole discipline is about: the
# test would scan nothing and pass. Fail at CONFIGURE time instead, where a human
# is watching, rather than shipping a green test with no subject. THIS PROPERTY
# IS WHY THE GATE SURFACED DURING THE MOVE AT ALL.
if(_sim_count LESS 4)
  message(
    FATAL_ERROR
      "game_sim_layers_link_scene_core found ${_sim_count} sim CMakeLists — "
      "the glob no longer matches the games layout, so the test would scan "
      "nothing. Fix the pattern in tests/CMakeLists.txt.")
endif()
list(JOIN _sim_cmake "' '" _sim_args)

# `[ $? -eq 1 ]` rather than `! grep`: a path that moved makes grep exit 2, and
# `!` would turn that error into a pass.
aether_add_shell_test(
  game_sim_layers_link_scene_core
  "grep -nE '^[^#]*aether::(scene|render)\\b' '${_sim_args}'; [ $? -eq 1 ]")

# Both are pure greps over the source tree — headless by definition. The label is
# not decoration: `pixi run test-headless` filters on it, and an unlabelled gate
# is simply absent from every headless run. Guarded because `aether_add_shell_test`
# registers NOTHING when no POSIX shell was found, and set_tests_properties on a
# test that does not exist is a hard CMake error.
if(TEST game_layer_layout)
  set_tests_properties(game_layer_layout game_sim_layers_link_scene_core
                       PROPERTIES LABELS headless)
endif()
