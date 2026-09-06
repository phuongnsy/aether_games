#include "cr/view/renderer.hpp"

#include <array>

#include "aether/core/math/math.hpp"
#include "aether/platform/window.hpp"  // ctx.window: backbuffer size for upscale
#include "aether/render/camera.hpp"
#include "aether/render/renderer.hpp"
#include "aether/rhi/command_list.hpp"
#include "aether/rhi/device.hpp"
#include "cr/view/shader_load.hpp"

namespace game {

Result<void> Renderer::LoadPipeline(const app::AppContext& ctx,
                                    const char* shader_dir) {
  auto load_prog = [&](const char* fs, ProgramHandle& dst) -> Result<void> {
    auto p = LoadProgram(ctx.device, shader_dir, fs);
    if (!p) {
      return std::unexpected(p.error());
    }
    dst = *p;
    return {};
  };
  auto make_u = [&](const char* n, UniformHandle& dst) -> Result<void> {
    auto u = ctx.device.CreateUniform(n, rhi::UniformType::kVec4, 1);
    if (!u) {
      return std::unexpected(u.error());
    }
    dst = *u;
    return {};
  };
  for (const auto& [fs, prog] : {
           std::pair{"color_grade.fs.bin", &grade_prog_},
           std::pair{"vignette.fs.bin", &vignette_prog_},
           std::pair{"chromatic_aberration.fs.bin", &chroma_prog_},
           std::pair{"bloom_threshold.fs.bin", &bloom_threshold_prog_},
           std::pair{"blur.fs.bin", &blur_prog_},
           std::pair{"scaled.fs.bin", &scaled_prog_},
       }) {
    if (auto r = load_prog(fs, *prog); !r) {
      return r;
    }
  }
  for (const auto& [n, u] : {
           std::pair{"u_grade", &grade_u_},
           std::pair{"u_vignette", &vignette_u_},
           std::pair{"u_chroma", &chroma_u_},
           std::pair{"u_bloom", &bloom_u_},
           std::pair{"u_blur", &blur_u_},
           std::pair{"u_scale", &scale_u_},
       }) {
    if (auto r = make_u(n, *u); !r) {
      return r;
    }
  }
  // P6 deferred lighting materials: a light draws as an additive quad reading
  // the normal G-buffer. u_light = height/strength/falloff; u_hero = caster.
  if (auto r = load_prog("light_bump.fs.bin", light_bump_prog_); !r) {
    return r;
  }
  if (auto r = load_prog("light_shadow.fs.bin", light_shadow_prog_); !r) {
    return r;
  }
  if (auto r = make_u("u_light", light_u_); !r) {
    return r;
  }
  if (auto r = make_u("u_hero", hero_u_); !r) {
    return r;
  }
  const Vec4 light_default{0.6f, 1.0f, 1.0f, 0.0f};  // height, strength, pow
  auto bump = ctx.renderer.CreateMaterial(light_bump_prog_, {&light_u_, 1},
                                          {&light_default, 1});
  if (!bump) {
    return std::unexpected(bump.error());
  }
  bump_material_ = *bump;
  const std::array<UniformHandle, 2> shadow_u{light_u_, hero_u_};
  const Vec4 hero_default{0.5f, 0.5f, 0.2f, 12.0f};
  const std::array<Vec4, 2> shadow_defaults{light_default, hero_default};
  auto shadow = ctx.renderer.CreateMaterial(light_shadow_prog_, shadow_u,
                                            shadow_defaults);
  if (!shadow) {
    return std::unexpected(shadow.error());
  }
  shadow_material_ = *shadow;
  return {};
}

// P0 post-processing: thread `scene_color` through deferred-lighting + bloom +
// the effect stack (grade → vignette → chromatic aberration) to the backbuffer.
void Renderer::Render(const app::AppContext& ctx, TextureHandle scene_color,
                      Vec3 ambient, bool night) {
  const Viewport vp{.width = ctx.render_size.width,
                    .height = ctx.render_size.height};
  if (auto r = post_chain_.Resize(ctx.device, ctx.render_size); !r) {
    return;  // degrade gracefully: skip post this frame
  }
  // Checked, not discarded: a target set that cannot fit leaves these
  // half-built, and rendering through them faults the GPU rather than looking
  // wrong.
  if (auto r = bloom_.Resize(ctx.device, ctx.render_size); !r) {
    return;
  }
  if (auto r = lighting_.Resize(ctx.device, ctx.render_size); !r) {
    return;
  }
  // A resize releases the sets for one frame before reallocating them (see
  // PostChain::Resize). The chain is the only writer to the backbuffer, so pass
  // the scene straight through rather than presenting a black frame.
  if (!post_chain_.Ready()) {
    ctx.renderer.Blit(scene_color);
    return;
  }

  // P6: dynamic 2D lighting FIRST — multiply the scene by an accumulated light
  // map so it's dark except where lit; before bloom so the light cores bleed.
  const render::LightingConfig light_cfg{.copy = scaled_prog_,
                                         .u_scale = scale_u_,
                                         .bump_material = bump_material_,
                                         .shadow_material = shadow_material_,
                                         .ambient = ambient};  // P7 day/night
  // P6-C: aim the shadow-casting light — project it to screen uv and feed the
  // shadow material's u_hero so its ray-march knows where the light is.
  for (const Light& l : frame_lights_) {
    if (!l.casts_shadows) {
      continue;
    }
    const render::ViewProjection vpm = render::ComputeViewProjection(
        frame_view_, ctx.device.HomogeneousDepth());
    const Vec3 ndc = TransformPoint(vpm.projection * vpm.view,
                                    Vec3{l.position.x, l.position.y, 0.0f});
    const Vec4 u_light{0.6f, 1.0f, 1.0f, 0.0f};
    // u_hero.y flips NDC-up → the shader's gl_FragCoord screen-uv (v=0 top);
    // without it the shadow's Y is inverted. .w = softness (lower=harder).
    const Vec4 u_hero{ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, 0.2f, 3.5f};
    const std::array<Vec4, 2> vals{u_light, u_hero};
    ctx.renderer.SetMaterialValues(shadow_material_, vals);
    break;
  }
  const TextureHandle lit_scene =
      lighting_.Compose(ctx.device, ctx.renderer, scene_color, frame_view_,
                        frame_lights_, frame_items_, light_cfg);

  // P1: bloom next — bright areas (the emissive coins + light cores) bleed —
  // then grade → vignette → chromatic aberration over the result.
  const render::BloomConfig bloom_cfg{.threshold = bloom_threshold_prog_,
                                      .u_bloom = bloom_u_,
                                      .threshold_knee = Vec2{0.55f, 0.2f},
                                      .blur = blur_prog_,
                                      .u_blur = blur_u_,
                                      .blur_radius = 2.0f,
                                      .blur_iterations = 2,
                                      .scaled = scaled_prog_,
                                      .u_scale = scale_u_,
                                      .intensity = 1.5f};
  const TextureHandle lit =
      bloom_.Compose(ctx.device, ctx.renderer, lit_scene, vp, bloom_cfg);

  // P7 runtime shader switching: the first post step swaps its PROGRAM with the
  // time of day — a cool colour-grade after dark, a neutral passthrough by day.
  const Vec4 night_grade{0.9f, 1.12f, 0.72f, 0.0f};  // dim · contrast · cool
  const Vec4 unit{1.0f, 0.0f, 0.0f, 0.0f};           // scaled passthrough
  const rhi::UniformBinding gb{.handle = grade_u_, .values = &night_grade};
  const rhi::UniformBinding sb{.handle = scale_u_, .values = &unit};
  const render::PostStep grade_step =
      night ? render::PostStep{.program = grade_prog_, .uniforms = {&gb, 1}}
            : render::PostStep{.program = scaled_prog_, .uniforms = {&sb, 1}};

  const Vec4 vignette{0.55f, 0.4f, 0.30f, 0.0f};  // start, softness, intensity
  const Vec4 chroma{0.004f, 0.0f, 0.0f, 0.0f};    // amount
  const rhi::UniformBinding vb{.handle = vignette_u_, .values = &vignette};
  const rhi::UniformBinding cb{.handle = chroma_u_, .values = &chroma};
  const std::array<render::PostStep, 3> steps{{
      grade_step,
      {.program = vignette_prog_, .uniforms = {&vb, 1}},
      {.program = chroma_prog_, .uniforms = {&cb, 1}},
  }};
  // Final composite upscales the resolution-scaled chain to the full
  // backbuffer.
  const Viewport backbuffer{.width = ctx.window.FramebufferSize().width,
                            .height = ctx.window.FramebufferSize().height};
  post_chain_.Run(ctx.device, ctx.renderer, lit, vp, steps, backbuffer);
}

}  // namespace game
