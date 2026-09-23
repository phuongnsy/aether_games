// Weather feature — the SIM half of lantern's 3D weather (weather-3d t4): rain
// over the spire, snow settling on its platforms. Owns the fields and consumes
// its own impacts; the view Emits them — the sim never renders. The 3D twin of
// coin_rush's features/weather, wired the way weather3d_lab proved out.
#pragma once

#include <optional>
#include <vector>

#include "aether/core/field.hpp"
#include "aether/core/gpu_handles.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/scene_core/geometry_query.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/surface/accumulation_field3.hpp"
#include "aether/surface/wetness_field.hpp"
#include "aether/weather/rain_field3.hpp"
#include "aether/worldsim/sky_visibility.hpp"

namespace lantern::weather {

using namespace aether;  // NOLINT(google-build-using-namespace)

enum class Mode : U8 { kOff, kRain, kSnow };

// A splash is SIM state (spawned by an impact, aged by the fixed step) so a
// replayed run splashes identically; only its disc is presentation.
struct Splash {
  Vec3 position;
  Vec3 normal;
  F32 age = 0.0f;
};
inline constexpr F32 kSplashLife = 0.35f;

class WeatherSim {
 public:
  // Build the producers ONCE (app's Load, after the world is instantiated so
  // the geometry query snapshots real colliders). The spawn rect frames the
  // spire's ground slab; drops below kFallY die with the walker's rule.
  // Textures arrive as OPAQUE handles — the view owns the assets, the sim
  // never touches the device (AGENTS.md law 4).
  void Setup(scene::Scene& scene, Mode mode, U64 seed, TextureHandle rain_tex,
             TextureHandle snow_tex);

  // Register one platform TOP as a surface: wetness keys on `surface` (the
  // collider node the impacts report) and snow accumulates on the patch grid.
  void RegisterPlatform(U64 surface, Vec3 origin, Vec3 axis_u, Vec3 axis_v,
                        U32 nu, U32 nv);

  // One fixed step: refresh the collider snapshot, step the active producer,
  // resolve impacts into wetness/snow/splashes, age and decay.
  void Step(F32 dt);

  void SetMode(Mode mode);

  // Slide BOTH producers' spawn boxes to follow the view, so precipitation
  // surrounds the player rather than hanging over one patch of world. The
  // shelter probe is per-column, so sheltering keeps working wherever it sits.
  void SetViewCenter(Vec2 center_xz);
  [[nodiscard]] Mode GetMode() const { return mode_; }

  // Read-only access for the view's Emit. Null until Setup runs.
  [[nodiscard]] const aether::weather::RainField3* Rain() const {
    return rain_ ? &*rain_ : nullptr;
  }
  [[nodiscard]] const aether::weather::RainField3* Snow() const {
    return snow_ ? &*snow_ : nullptr;
  }
  [[nodiscard]] const surface::AccumulationField3& Accum() const {
    return accum_;
  }
  [[nodiscard]] const surface::WetnessField& Wetness() const {
    return wetness_;
  }
  [[nodiscard]] const std::vector<Splash>& Splashes() const {
    return splashes_;
  }

 private:
  Mode mode_ = Mode::kOff;
  std::optional<scene::SceneGeometryQuery3> query_;
  std::optional<worldsim::SkyVisibilityField3> sky_;
  std::optional<aether::weather::RainField3> rain_;
  std::optional<aether::weather::RainField3> snow_;
  surface::WetnessField wetness_;
  surface::AccumulationField3 accum_;
  // Sub-zero and constant: dusk on the spire, so settled snow persists.
  ValueField<F32> cold_{-5.0f};
  std::vector<aether::weather::Impact3> impacts_;
  std::vector<Splash> splashes_;
};

}  // namespace lantern::weather
