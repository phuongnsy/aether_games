// Coin Rush level set — the API over the DECLARATIVE level files (compiled by
// tools/gen_content.py). Systems ask by index; they never parse a file.
#pragma once

#include <span>
#include <string_view>

#include "aether/core/types.hpp"

namespace game {

// Per-level weather (rain rate + wind + snow/fog), defined by the level data.
struct WeatherConfig {
  aether::F32 rain_rate = 260.0f;
  aether::F32 wind_base = -28.0f;
  aether::F32 gust = 60.0f;
  bool snow = false;       // Whiteout: falling + accumulating snow
  aether::F32 fog = 0.0f;  // 0-1 white-haze intensity
};

[[nodiscard]] int LevelCount();
// The tile map for `index` (clamped): '#' solid, '.' sky, 'o' coin, 'P' spawn,
// 'E' exit. Row 0 is the TOP.
[[nodiscard]] std::span<const std::string_view> LevelMap(int index);
[[nodiscard]] WeatherConfig LevelWeather(int index);
[[nodiscard]] std::string_view LevelName(int index);

}  // namespace game
