# coin_rush_tests — the game's per-feature unit suites (headless, deterministic),
# registered with CTest. Separate from the engine's aether_tests so game logic no
# longer compiles into the shared engine suite. Each feature adds its tests/
# sources here as it lands.
find_package(doctest CONFIG REQUIRED)

add_executable(
  coin_rush_tests
  main.cpp
  headless_sim_test.cpp
  content_test.cpp
  replay_test.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../features/movement/tests/movement_test.cpp")
set_target_properties(
  coin_rush_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                             "${CMAKE_BINARY_DIR}/bin/tests")
target_link_libraries(
  coin_rush_tests
  PRIVATE aether_warnings
          coin_rush::feat_movement
          coin_rush::feat_coins
          coin_rush::feat_progression
          coin_rush::feat_weather
          coin_rush::content
          doctest::doctest)

include(doctest)
aether_discover_tests(coin_rush_tests)
set_property(
  DIRECTORY
  APPEND
  PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/label_tests.cmake")

# --- dependency-law seam checks (mirror the engine's header-seam tests) -------
# The CMake target graph already makes an illegal cross-layer include a COMPILE
# error (a layer only sees its deps' PUBLIC include dirs). These greps guard the
# rules that scoping alone doesn't express, and document them.
set(_cr "${CMAKE_CURRENT_SOURCE_DIR}/..")

# The sim layer (runtime + features) must never use the RENDERER: no source or
# header may include aether/render/. (The pragmatic headless boundary — rhi is
# pulled transitively via scene, but the renderer never appears in sim.)
aether_add_shell_test(
  cr_sim_no_renderer
  "! grep -rnE '#[[:space:]]*include[[:space:]]*[<\"]aether/render/' '${_cr}/runtime' '${_cr}/features'"
)

# Features never depend UP on the view or app layers (they talk only to runtime +
# each other, via events + shared world state).
aether_add_shell_test(
  cr_features_no_view_or_app
  "! grep -rnE '#[[:space:]]*include[[:space:]]*[<\"]cr/(view|app)/' '${_cr}/features'"
)

# View consumes only the runtime snapshot/events — never a feature (spec §the
# dependency law). A match fails.
aether_add_shell_test(
  cr_view_no_features
  "! grep -rnE '#[[:space:]]*include[[:space:]]*[<\"]cr/features/' '${_cr}/view'"
)

# Pure greps over the source tree — headless by definition.
set_tests_properties(cr_sim_no_renderer cr_features_no_view_or_app
                     cr_view_no_features PROPERTIES LABELS headless)
