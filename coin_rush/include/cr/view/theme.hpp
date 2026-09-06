#pragma once

#include "aether/core/math/geometry.hpp"  // Rect
#include "aether/core/math/vec.hpp"

namespace game {

constexpr aether::Vec4 kUiPanel = aether::Hex(0x3e3546, 0.96f);
constexpr aether::Vec4 kUiButton = aether::Hex(0x484a77);
constexpr aether::Vec4 kUiButtonHover = aether::Hex(0x4d65b4);
constexpr aether::Vec4 kUiButtonActive = aether::Hex(0x2e222f);
constexpr aether::Vec4 kUiText = aether::Hex(0xc7dcd0);
constexpr aether::Vec4 kUiTextDim = aether::Hex(0x7f708a);
constexpr aether::Vec4 kUiAccent = aether::Hex(0xf9c22b);
constexpr aether::Vec4 kUiGood = aether::Hex(0x63c74d);
constexpr aether::Vec4 kUiBad = aether::Hex(0xe43b44);
constexpr aether::Vec4 kUiScrim = aether::Hex(0x100c14, 0.62f);
constexpr aether::Rect kFullUv{
    .x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};

}  // namespace game
