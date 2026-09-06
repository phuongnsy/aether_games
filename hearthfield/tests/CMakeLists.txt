# hearthfield_tests — the sim's unit suites, and at H0 the game's ONLY
# deliverable: "a headless smoke test before anything renders" is the milestone.
# Headless by construction — no device is created and no render library is
# linked, which is also the seam this proves (games/CLAUDE.md law 4).
find_package(doctest CONFIG REQUIRED)

add_executable(
  hearthfield_tests
  main.cpp
  tick_test.cpp
  plots_test.cpp
  offline_test.cpp
  replay_test.cpp
  pick_test.cpp
  save_test.cpp
  chain_test.cpp
  api_test.cpp
  livestock_test.cpp
  weather_test.cpp
  script_test.cpp
  archipelago_test.cpp
  farms_test.cpp
  camera_test.cpp
  islands_test.cpp
  loading_test.cpp
  placement_test.cpp
  # app/'s screens, so the POINTER -> verb path
  # is covered rather than only rendered.
  ../app/screens.cpp)
set_target_properties(
  hearthfield_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                               "${CMAKE_BINARY_DIR}/bin/tests")
target_link_libraries(
  hearthfield_tests
  PRIVATE aether_warnings
          doctest::doctest
          hf::runtime
          hf::content
          hf::feat_plots
          hf::feat_production
          hf::feat_livestock
          hf::feat_orders
          hf::feat_economy
          hf::feat_placement
          hf::view
          aether::ui
          # For the real-digitiser pinch fixture: a device recording is how
          # multi-touch contacts reach a machine with no touch hardware.
          aether::input)
# The v1 save fixture lives with the other binary compatibility fixtures under
# tests/assets/, and is tracked (see .gitignore) precisely because no current
# code produced it.
target_include_directories(hearthfield_tests
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app")
target_compile_definitions(
  hearthfield_tests
  PRIVATE
    AETHER_TEST_ASSET_DIR="${AETHER_ROOT}/tests/assets"
    HEARTHFIELD_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    # The island registry's half-extents are checked against the same
    # contract `models-check` gates, which chains them to the asset
    # without this suite having to open a .glb (islands_test.cpp).
    HEARTHFIELD_MODEL_CONTRACT="${CMAKE_CURRENT_SOURCE_DIR}/../../tools/model_contract.json")
aether_discover_tests(hearthfield_tests)
# The suite's cases join the `headless` label set at CTest time — see
# label_tests.cmake for why it cannot happen here.
set_property(
  DIRECTORY
  APPEND
  PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/label_tests.cmake")
