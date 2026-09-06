// Build a post-process program (shared sprite VS + an effect FS) from compiled
// .bin in shader_dir (passed in, not a macro, so cr_view stays macro-free).
#pragma once

#include "aether/core/error.hpp"
#include "aether/core/gpu_handles.hpp"
#include "aether/core/types.hpp"
#include "aether/platform/file_system.hpp"
#include "aether/rhi/device.hpp"

namespace game {

using namespace aether;  // NOLINT(google-build-using-namespace)

[[nodiscard]] inline Result<ProgramHandle> LoadProgram(rhi::Device& device,
                                                       const char* shader_dir,
                                                       const char* fs_file) {
  auto shaders = platform::CreateFileSystem(shader_dir);
  if (!shaders) {
    return std::unexpected(shaders.error());
  }
  auto vs = (*shaders)->Read("sprite.vs.bin");
  auto fs = (*shaders)->Read(fs_file);
  if (!vs) {
    return std::unexpected(vs.error());
  }
  if (!fs) {
    return std::unexpected(fs.error());
  }
  auto vsh = device.CreateShader(rhi::ShaderStage::kVertex, vs->data(),
                                 static_cast<U32>(vs->size()));
  auto fsh = device.CreateShader(rhi::ShaderStage::kFragment, fs->data(),
                                 static_cast<U32>(fs->size()));
  if (!vsh || !fsh) {
    return Fail(Errc::kInitFailed, "post shader create");
  }
  auto program = device.CreateProgram(*vsh, *fsh);
  device.Destroy(*vsh);
  device.Destroy(*fsh);
  return program;
}

}  // namespace game
