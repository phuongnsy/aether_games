// The scene graph the slice draws through, and the camera that looks at it.
//
// THE ONLY THING HERE THAT OWNS A SCENE. It is the view's, by the layer law:
// a sim layer may not name aether::scene, and the graph exists to be drawn.
//
// ORDER IS THE CONTRACT, and the app keeps it: StepCameraCollision resolves
// the distance limit, and only then does Update run the orbit driver that
// places the camera — §13.5.2's rule for game-driven bodies, read across to a
// camera. A stage that updated itself would take that choice away from the
// caller that has to make it.
#pragma once

#include "aether/app/app.hpp"
#include "aether/core/render_frame.hpp"
#include "aether/core/types.hpp"
#include "aether/scene_core/camera_component.hpp"
#include "aether/scene_core/orbit_component.hpp"
#include "aether/scene_core/scene.hpp"

namespace infiltration::view {

using namespace aether;  // NOLINT(google-build-using-namespace)

class Stage {
 public:
  // §17.2.2's LOOK-AT camera: it rotates about a target point and moves in
  // and out relative to it. Returns the orbit rig, because the caller drives
  // the pivot every step and resolves its collision before this updates.
  [[nodiscard]] scene::OrbitComponent* MakeCamera() {
  const scene::NodeId camera = scene_.CreateNode(scene_.Root());
  camera_node_ = camera;
  auto* view = scene_.AddComponent<scene::CameraComponent>(camera);
  view->projection = ProjectionMode::kPerspective;
  view->reference_size = 0.0f;
  view->near_z = 0.05f;
  view->far_z = 200.0f;
  scene_.SetActiveCamera(camera);
  orbit_ = scene_.AddComponent<scene::OrbitComponent>(camera);
  orbit_->pivot = Vec3{0.0f, 1.2f, 0.0f};
  orbit_->distance = 9.0f;
  orbit_->min_distance = 1.0f;  // see kCameraProbe: the level demands it
  orbit_->max_distance = 40.0f;
  orbit_->pitch = 0.42f;
    return orbit_;
  }

  void Update(F32 dt) { scene_.Update(dt); }

  [[nodiscard]] const scene::Node* Camera() const {
    return scene_.Get(camera_node_);
  }

  // The frame every draw then adds to: a render-size viewport and F4's debug
  // bounds flag, which an example got wrong by forgetting the second.
  [[nodiscard]] RenderFrame BuildFrame(const app::AppContext& ctx) {
    return scene_.BuildRenderFrame(
        Viewport{.width = ctx.render_size.width,
                 .height = ctx.render_size.height},
        ctx.ShowDebugBounds(), &ctx.jobs);
  }

  [[nodiscard]] scene::Scene& Graph() { return scene_; }

 private:
  scene::Scene scene_;
  scene::NodeId camera_node_{};
  scene::OrbitComponent* orbit_ = nullptr;
};

}  // namespace infiltration::view
