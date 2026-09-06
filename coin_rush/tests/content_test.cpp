// Content validation: every compiled level loads through the cr_content API and
// meets the structural contract (rectangular, one spawn, >=1 exit/coin).
#include <doctest/doctest.h>

#include <cstddef>
#include <string_view>

#include "cr/content/levels.hpp"

using namespace game;

TEST_CASE("content: every level loads with a valid structure") {
  REQUIRE(LevelCount() >= 1);
  for (int i = 0; i < LevelCount(); ++i) {
    CAPTURE(i);
    CHECK_FALSE(LevelName(i).empty());

    const std::span<const std::string_view> map = LevelMap(i);
    REQUIRE(map.size() > 0);
    const std::size_t width = map[0].size();
    int spawns = 0;
    int exits = 0;
    int coins = 0;
    for (const std::string_view row : map) {
      CHECK(row.size() == width);  // rectangular (gen_content's ragged guard)
      for (const char c : row) {
        spawns += (c == 'P');
        exits += (c == 'E');
        coins += (c == 'o');
      }
    }
    CHECK(spawns == 1);
    CHECK(exits >= 1);
    CHECK(coins >= 1);

    const WeatherConfig weather = LevelWeather(i);
    CHECK(weather.rain_rate >= 0.0f);
  }
}
