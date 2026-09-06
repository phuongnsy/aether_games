// The game's post pipeline (deferred-lighting + bloom + grade). Consumes ONLY a
// finished RenderFrame + the lit-scene texture, so the dependency law holds.
#pragma once

#include <vector>

#include "aether/app/app.hpp"
#include "aether/core/error.hpp"
#include "aether/core/gpu_handles.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/render/bloom.hpp"
#include "aether/render/lighting.hpp"
#include "aether/render/post_chain.hpp"

namespace game {

using namespace aether;  // NOLINT(google-build-using-namespace)

class Renderer {
 public:
  // Build the pipeline's GPU programs/uniforms/materials; shader_dir is the
  // app's AETHER_SHADER_DIR. Degrades via Result on shader-load failure.
  [[nodiscard]] Result<void> LoadPipeline(const app::AppContext& ctx,
                                          const char* shader_dir);

  // Stash this frame's camera + lights + items for the deferred passes (called
  // at the end of the app's Extract, which owns frame assembly).
  void Capture(const RenderFrame& frame) {
    frame_view_ = frame.view;
    frame_lights_ = frame.lights;
    frame_items_ = frame.items;
  }

  // Deferred lighting → bloom → grade/vignette/chroma over `scene_color`.
  // `ambient` + `night` come from the app's day/night cycle.
  void Render(const app::AppContext& ctx, TextureHandle scene_color,
              Vec3 ambient, bool night);

 private:
  render::Bloom bloom_;
  render::PostChain post_chain_;
  render::Lighting lighting_;  // deferred 2D lighting (normal maps + shadows)

  RenderView frame_view_{};              // captured in Capture, used in Render
  std::vector<Light> frame_lights_;      // this frame's lights
  std::vector<Renderable> frame_items_;  // this frame's items (normal G-buffer)

  // Deferred light-accumulation shaders/materials (shader_studio).
  ProgramHandle light_bump_prog_;
  ProgramHandle light_shadow_prog_;
  UniformHandle light_u_;  // u_light: height, normal strength, falloff power
  UniformHandle hero_u_;   // u_hero: caster screen pos + softness (shadow mat)
  MaterialHandle bump_material_;
  MaterialHandle shadow_material_;
  // Post chain (shader_studio): bloom → grade → vignette → chromatic
  // aberration.
  ProgramHandle grade_prog_;
  ProgramHandle vignette_prog_;
  ProgramHandle chroma_prog_;
  ProgramHandle bloom_threshold_prog_;
  ProgramHandle blur_prog_;
  ProgramHandle scaled_prog_;
  UniformHandle grade_u_;
  UniformHandle vignette_u_;
  UniformHandle chroma_u_;
  UniformHandle bloom_u_;
  UniformHandle blur_u_;
  UniformHandle scale_u_;
};

}  // namespace game
