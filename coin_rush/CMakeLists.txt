# ONE include root for the whole game, mirroring the engine's single
# ${AETHER_INCLUDE_DIR}: public headers live at games/coin_rush/include/,
# private ones stay in each layer's src/. A layer's PUBLIC surface is a
# property of the game, not of the directory it happens to be built from.
set(COIN_RUSH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Coin Rush — the M8 vertical-slice game (its own app::Game, separate from the
# sandbox feature-gallery). Fully self-contained assets: its own art/audio/fonts
# under assets/ (gen_assets → typed constants) AND its own shader set under
# shaders/ (compiled here, not borrowed from the sandbox). The shader sources are
# duplicated from the sandbox on purpose — a downstream game owns its render
# assets and must not depend on the engine's sample build.

# --- generate the strongly-typed asset catalog (configure time) -------------
set(_assets_hpp "${CMAKE_CURRENT_SOURCE_DIR}/generated/assets.hpp")
find_program(AETHER_PYTHON NAMES python3 python REQUIRED)
execute_process(
  COMMAND
    "${AETHER_PYTHON}" "${AETHER_ROOT}/tools/gen_assets.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${_assets_hpp}" --namespace assets
  RESULT_VARIABLE _gen_assets_rc)
if(NOT _gen_assets_rc EQUAL 0)
  message(FATAL_ERROR "gen_assets.py failed for game (${_gen_assets_rc})")
endif()

# --- compile the game's OWN .sc shaders to SPIR-V ---------------------------
# Duplicated from the sandbox (deliberately — see the header): the game owns its
# render assets and builds them into its OWN output dir, so it never depends on
# sandbox_shaders. Only the shaders Coin Rush actually uses are here.
find_package(bgfx CONFIG REQUIRED) # shaderc + shader includes only (not linked)
find_program(
  BGFX_SHADERC
  NAMES shaderc
  HINTS "${bgfx_PACKAGE_FOLDER_RELEASE}/bin" "${bgfx_PACKAGE_FOLDER_DEBUG}/bin")
set(_cr_sh_inc "")
foreach(_bf "${bgfx_PACKAGE_FOLDER_RELEASE}" "${bgfx_PACKAGE_FOLDER_DEBUG}")
  if(_bf AND EXISTS "${_bf}/shaderc_include")
    set(_cr_sh_inc "${_bf}/shaderc_include")
  endif()
endforeach()
if(NOT BGFX_SHADERC OR NOT _cr_sh_inc)
  message(
    FATAL_ERROR
      "bgfx shaderc/includes not found for coin_rush "
      "(SHADERC=${BGFX_SHADERC}, inc=${_cr_sh_inc}); "
      "re-run `pixi run configure`.")
endif()

set(_cr_shader_src "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
# Named after the PROFILE rather than hardcoded `spirv` — see sandbox's copy of
# this line for why. AETHER_SHADER_DIR below carries whatever this resolves to,
# so nothing at runtime knows the difference.
set(_cr_shader_out
    "${CMAKE_BINARY_DIR}/bin/res/coin_rush/shaders/${AETHER_SHADERC_PROFILE}")
set(_cr_varying "${_cr_shader_src}/varying.def.sc")
set(_cr_shader_bins "")

# coin_rush_compile_shader(<out-basename> <vertex|fragment> <src.sc>): emits
# <out>/<basename>.bin, appends to _cr_shader_bins. (Distinct name from the
# sandbox's function — CMake functions are global.)
function(coin_rush_compile_shader OUT TYPE SRC)
  set(_out "${_cr_shader_out}/${OUT}.bin")
  add_custom_command(
    OUTPUT "${_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_cr_shader_out}"
    COMMAND
      ${BGFX_SHADERC} -f "${_cr_shader_src}/${SRC}" -o "${_out}" --type ${TYPE}
      --platform ${AETHER_SHADERC_PLATFORM} -p ${AETHER_SHADERC_PROFILE}
      --varyingdef "${_cr_varying}" -i "${_cr_sh_inc}"
    DEPENDS "${_cr_shader_src}/${SRC}" "${_cr_varying}"
    COMMENT "shaderc coin_rush/${OUT}.bin"
    VERBATIM)
  set(_cr_shader_bins
      ${_cr_shader_bins} "${_out}"
      PARENT_SCOPE)
endfunction()

coin_rush_compile_shader(sprite.vs vertex sprite.vs.sc
)# engine default + game VS
coin_rush_compile_shader(sprite.fs fragment sprite.fs.sc
)# engine default sprite
coin_rush_compile_shader(color_grade.fs fragment color_grade.fs.sc)
coin_rush_compile_shader(vignette.fs fragment vignette.fs.sc)
coin_rush_compile_shader(chromatic_aberration.fs fragment
                         chromatic_aberration.fs.sc)
coin_rush_compile_shader(bloom_threshold.fs fragment bloom_threshold.fs.sc)
coin_rush_compile_shader(blur.fs fragment blur.fs.sc)
coin_rush_compile_shader(scaled.fs fragment scaled.fs.sc)
coin_rush_compile_shader(glow.fs fragment glow.fs.sc) # coins (emissive)
coin_rush_compile_shader(scroll.fs fragment scroll.fs.sc) # floor + bg
coin_rush_compile_shader(copy.fs fragment copy.fs.sc) # wall normal passthrough
coin_rush_compile_shader(light_bump.fs fragment light_bump.fs.sc)
coin_rush_compile_shader(light_shadow.fs fragment light_shadow.fs.sc)
add_custom_target(coin_rush_shaders ALL DEPENDS ${_cr_shader_bins})

# --- layered libraries (see AGENTS.md) --------------------------------
add_subdirectory(content) # declarative level set (gen_content → typed data)
add_subdirectory(runtime) # sim foundation (GameWorld, event bus, snapshot)
add_subdirectory(features/movement) # the kinematic controller feature slice
add_subdirectory(features/coins) # coin pickup detection feature slice
add_subdirectory(features/progression) # round rules (timer, unlock, win)
add_subdirectory(features/weather) # weather sim (rain/snow/accum → wetness)
add_subdirectory(view) # presentation: hud + vfx glue (cr_view)

# app/ is the composition root: the app::Game orchestrator, the flow screens, and
# main. It sees everything (links every layer).
set(_cr_app_sources app/coin_rush.cpp app/screens.cpp app/input_source.cpp)
# The ENTRY POINT is per-platform, everything else is shared. Web is still an
# executable (emcc emits coin_rush.js + coin_rush.wasm from one), so unlike
# Android it needs no second target — only a different `main`.
if(EMSCRIPTEN)
  set(_cr_main app/main_web.cpp)
else()
  set(_cr_main app/main.cpp)
endif()
add_executable(coin_rush ${_cr_main} ${_cr_app_sources})
# The game executable; its test target goes to bin/tests with every other
# test binary.
set_target_properties(coin_rush PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                           "${CMAKE_BINARY_DIR}/bin/games")
set(_cr_app_targets coin_rush)

# Android needs a SHARED LIBRARY, because NativeActivity dlopen()s the game
# rather than exec()ing it — an executable is not loadable that way. The desktop
# executable is kept alongside it deliberately: it is what runs headless under
# `adb shell`, which is how tier 1 is verified, and the two differ by ONE source
# file (the entry point) with the other three shared.
if(ANDROID)
  add_library(coin_rush_apk SHARED app/main_android.cpp ${_cr_app_sources})
  # libcoin_rush.so is the name the manifest's android.app.lib_name refers to,
  # so the target name must not leak into the artifact.
  set_target_properties(
    coin_rush_apk
    PROPERTIES OUTPUT_NAME coin_rush
               LIBRARY_OUTPUT_DIRECTORY
               "${CMAKE_BINARY_DIR}/bin/android/${ANDROID_ABI}")
  # The glue only belongs on THIS target: it defines ANativeActivity_onCreate and
  # references android_main, which the desktop executable does not have.
  target_link_libraries(coin_rush_apk PRIVATE aether::android_glue)
  list(APPEND _cr_app_targets coin_rush_apk)
endif()

# WHERE THE BINARY LOOKS FOR ITS DATA. On every native platform these are the
# build machine's own directories; on the web they are MEMFS MOUNT POINTS, and
# the trees are packed into the module below. Tier 1 dodged this with
# `-sNODERAWFS=1` and said the line would have to change; this is that change.
if(EMSCRIPTEN)
  set(_cr_baked_assets "/assets")
  set(_cr_baked_shaders "/shaders")
  set(_cr_web_target coin_rush)
else()
  set(_cr_baked_assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  set(_cr_baked_shaders "${_cr_shader_out}")
endif()

# Looped rather than repeated: two targets that must agree about every library,
# include path and baked path, and a drifting pair is worse than a loop.
foreach(_cr_target IN LISTS _cr_app_targets)
  target_link_libraries(
    ${_cr_target}
    PRIVATE aether_warnings
            aether::app
            coin_rush::runtime
            coin_rush::content
            coin_rush::view
            coin_rush::feat_movement
            coin_rush::feat_coins
            coin_rush::feat_progression
            coin_rush::feat_weather)
  target_include_directories(
    ${_cr_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app"
                          "${CMAKE_CURRENT_SOURCE_DIR}/generated")
  target_compile_definitions(
    ${_cr_target} PRIVATE AETHER_SHADER_DIR="${_cr_baked_shaders}"
                          AETHER_ASSET_DIR="${_cr_baked_assets}")
  add_dependencies(${_cr_target} coin_rush_shaders) # the game's OWN shaders
endforeach()

# --- the web package: assets PRELOADED into MEMFS ---------------------------
# A browser has no filesystem, so the two trees are packed into the `.data` blob
# emcc emits beside the wasm and mounted at the paths baked above. GEA §7.2.2
# describes exactly this and why it costs nothing here: virtual files inside a
# composite archive "remember their relative paths", so the tree "looks like a
# raw file system" and `resources` never learns the difference. On the web the
# load-time argument he makes for discs is stronger still — a per-file open is an
# HTTP round trip.
#
# Only THIS target gets a package. The sandbox and the other two games link for
# wasm but ship no assets, because tier 2's deliverable is one playable game and
# a `.data` blob per binary is real download weight.
if(EMSCRIPTEN)
  target_link_options(
    ${_cr_web_target}
    PRIVATE
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@${_cr_baked_assets}"
    "SHELL:--preload-file ${_cr_shader_out}@${_cr_baked_shaders}")
endif()

add_subdirectory(tests) # coin_rush_tests (per-feature unit suites, CTest)
