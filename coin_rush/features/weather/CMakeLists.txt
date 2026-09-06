# coin_rush weather feature — the sim half of the weather (rain/snow producers +
# accumulation + wetness). A SIM feature: runtime + content (WeatherConfig) +
# core; the weather/surface/scene/worldsim headers come transitively via runtime.
# Rendering of the particles stays in view (it Emits from the read-only fields).
add_library(cr_feat_weather STATIC src/system.cpp)
add_library(coin_rush::feat_weather ALIAS cr_feat_weather)

target_include_directories(cr_feat_weather PUBLIC ${COIN_RUSH_INCLUDE_DIR})
target_link_libraries(
  cr_feat_weather
  PUBLIC coin_rush::runtime coin_rush::content aether::core
  PRIVATE aether_warnings)
