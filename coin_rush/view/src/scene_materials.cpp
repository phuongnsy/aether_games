#include "cr/view/scene_materials.hpp"

#include <cmath>

#include "assets.hpp"  // generated typed asset constants (gen_assets)
#include "cr/view/shader_load.hpp"

namespace game {

namespace {
// Parallax-background feel (moved off the orchestrator with its material).
constexpr F32 kBgTileWorld = 140.0f;  // world units per starfield tile
constexpr F32 kParallax = 0.15f;      // background UV shift per world unit
constexpr F32 kBgDrift = 0.015f;      // ambient UV drift per second
}  // namespace

Result<void> SceneMaterials::Load(const app::AppContext& ctx,
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

  // The coins' custom shader: the Renderer draws coin batches through this
  // program instead of the default sprite shader; u_glow pulses each frame.
  if (auto r = load_prog("glow.fs.bin", glow_prog_); !r) {
    return r;
  }
  if (auto r = make_u("u_glow", glow_u_); !r) {
    return r;
  }
  const Vec4 glow_default{0.7f, 0.0f, 0.0f, 0.0f};  // x=intensity, y=pulse
  auto mat = ctx.renderer.CreateMaterial(glow_prog_, {&glow_u_, 1},
                                         {&glow_default, 1});
  if (!mat) {
    return std::unexpected(mat.error());
  }
  coin_material_ = *mat;

  // P3: a scrolling-UV material for the floor (an animated surface).
  if (auto r = load_prog("scroll.fs.bin", scroll_prog_); !r) {
    return r;
  }
  if (auto r = make_u("u_scroll", scroll_u_); !r) {
    return r;
  }
  const Vec4 scroll_default{10.5f, 10.0f, 0.0f, 0.0f};
  auto floor_mat = ctx.renderer.CreateMaterial(scroll_prog_, {&scroll_u_, 1},
                                               {&scroll_default, 1});
  if (!floor_mat) {
    return std::unexpected(floor_mat.error());
  }
  floor_material_ = *floor_mat;

  // The parallax background reuses the scroll shader with its own material (own
  // u_scroll value), driven by camera motion + ambient drift.
  auto bg_mat = ctx.renderer.CreateMaterial(scroll_prog_, {&scroll_u_, 1},
                                            {&scroll_default, 1});
  if (!bg_mat) {
    return std::unexpected(bg_mat.error());
  }
  bg_material_ = *bg_mat;

  // P6-B normal maps: floor/wall surfaces carry a normal texture for relief.
  // Walls' normal alpha = 1 (occluder) via a copy material.
  if (auto r = load_prog("copy.fs.bin", copy_prog_); !r) {
    return r;
  }
  auto floor_n =
      ctx.resources.Load<resources::Texture>(assets::effects::kFloorNormal);
  auto wall_n =
      ctx.resources.Load<resources::Texture>(assets::effects::kWallNormal);
  if (!floor_n) {
    return std::unexpected(floor_n.error());
  }
  if (!wall_n) {
    return std::unexpected(wall_n.error());
  }
  floor_normal_ = *floor_n;
  wall_normal_ = *wall_n;
  (void)ctx.renderer.SetMaterialTextures(floor_material_,
                                         {.normal = floor_normal_->Handle()});
  auto wall_mat = ctx.renderer.CreateMaterial(copy_prog_, {}, {});
  if (!wall_mat) {
    return std::unexpected(wall_mat.error());
  }
  wall_material_ = *wall_mat;
  (void)ctx.renderer.SetMaterialTextures(wall_material_,
                                         {.normal = wall_normal_->Handle()});
  return {};
}

void SceneMaterials::Animate(render::Renderer& renderer, F32 elapsed,
                             Vec2 cam_pos) {
  // Pulse in [0.55, 1.0] so the coins always keep some glow (and always feed
  // the bloom), just breathing brighter/dimmer rather than fading out.
  const F32 pulse = 0.78f + 0.22f * std::sin(elapsed * 3.5f);
  const Vec4 glow{0.7f, pulse, 0.0f, 0.0f};
  renderer.SetMaterialValues(coin_material_, {&glow, 1});
  const Vec4 floor_scroll{std::fmod(elapsed * 0.12f, 1.0f),
                          std::fmod(elapsed * 0.08f, 1.0f), 0.0f, 0.0f};
  renderer.SetMaterialValues(floor_material_, {&floor_scroll, 1});
  // Parallax background: UV shifts by a FRACTION of camera motion (so it reads
  // as distant) plus a slow ambient drift.
  const Vec4 bg_scroll{
      cam_pos.x * kParallax / kBgTileWorld + elapsed * kBgDrift,
      cam_pos.y * kParallax / kBgTileWorld + elapsed * kBgDrift, 0.0f, 0.0f};
  renderer.SetMaterialValues(bg_material_, {&bg_scroll, 1});
}

}  // namespace game
