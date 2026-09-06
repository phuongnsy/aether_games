// The game's per-object materials (coin glow, floor/bg scroll, wall/floor
// normals): created + animated here, handed to BuildArena to attach to sprites.
#pragma once

#include "aether/app/app.hpp"
#include "aether/core/error.hpp"
#include "aether/core/gpu_handles.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/render/renderer.hpp"
#include "aether/resources/resource_handle.hpp"
#include "aether/resources/texture.hpp"

namespace game {

using namespace aether;  // NOLINT(google-build-using-namespace)

class SceneMaterials {
 public:
  // Build the per-object programs/uniforms/materials + load the normal maps;
  // shader_dir = AETHER_SHADER_DIR.
  [[nodiscard]] Result<void> Load(const app::AppContext& ctx,
                                  const char* shader_dir);

  // Per-frame material animation: pulse the coin glow, drift the floor's + the
  // parallax background's scrolling UVs (cam_pos drives the parallax depth).
  void Animate(render::Renderer& renderer, F32 elapsed, Vec2 cam_pos);

  // Handles for scene authoring (BuildArena) to attach to sprite components.
  [[nodiscard]] MaterialHandle Coin() const { return coin_material_; }
  [[nodiscard]] MaterialHandle Floor() const { return floor_material_; }
  [[nodiscard]] MaterialHandle Bg() const { return bg_material_; }
  [[nodiscard]] MaterialHandle Wall() const { return wall_material_; }

 private:
  ProgramHandle glow_prog_;  // coins: emissive-glow (u_glow pulses)
  UniformHandle glow_u_;
  MaterialHandle coin_material_;
  ProgramHandle scroll_prog_;  // floor + background: scrolling UVs
  UniformHandle scroll_u_;
  MaterialHandle floor_material_;
  MaterialHandle bg_material_;
  ProgramHandle copy_prog_;       // wall: albedo passthrough carrying a normal
  MaterialHandle wall_material_;  // occluder (normal alpha = 1) for shadows
  resources::ResourceHandle<resources::Texture> floor_normal_;
  resources::ResourceHandle<resources::Texture> wall_normal_;
};

}  // namespace game
