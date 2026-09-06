// The farm's soundscape: GEA §14.4's model, wired to this game's two sounds.
//
// TWO KINDS OF SOUND, AND THE BOOK DRAWS THE LINE. §14.4.1 separates 3D sounds
// — which "emanate from all over the game world" and get spatialised — from 2D
// sounds "designed to be played 'directly' in the speakers", and it lists
// "ambient sounds like wind or rainfall" as the example. So the mill is a
// positioned source and the rain is not: rain has no direction, because it is
// everywhere the listener is.
//
// WHERE THE LISTENER GOES IS THE INTERESTING PART, and it is ADR-0081's second
// silent-failure mode. Under a parallel projection the camera's DISTANCE
// changes nothing you can see, so the eye sits 40 m back by authoring accident
// and any other value would render identically. Attach the listener there and
// every sound on the farm is 40 m away — uniformly attenuated, barely panned,
// and wrong in a way that sounds like a volume bug rather than a wiring one.
//
// It goes at what the camera LOOKS AT instead, oriented like the camera. GEA
// §14.4.3.3 is the authority for departing from the obvious reading: Naughty
// Dog bent the fall-off curve because intelligibility mattered more than
// physics, and the section ends "Never be afraid to do whatever it takes to
// satisfy the needs of your game."
#pragma once

#include "aether/audio/audio_system.hpp"
#include "aether/core/math/quat.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/resources/audio_clip.hpp"
#include "aether/resources/resource_manager.hpp"
#include "hf/runtime/snapshot.hpp"

namespace hearthfield::view {

// Where the mill is heard from, in metres. `min` is roughly the building
// itself; `max` is a couple of board-widths, so crossing it is a fade rather
// than an edge.
inline constexpr aether::F32 kMillFalloffMin = 2.0f;
inline constexpr aether::F32 kMillFalloffMax = 22.0f;

// The listener, from an orbit camera. FREE OF THE AUDIO SYSTEM on purpose: it
// is the one piece of §3g anybody could get wrong, and this way it is a pure
// function that a headless test can check without a device.
//
// `target` is what the camera orbits, which under kOrthographic3D is the only
// point on the view axis with any meaning.
[[nodiscard]] aether::audio::Listener ListenerFor(aether::Vec3 target,
                                                  aether::F32 yaw,
                                                  aether::F32 pitch);

class FarmAudio {
 public:
  // Loads both clips. Missing audio costs the sound and nothing else — the
  // same bargain lantern's chime makes, and for the same reason.
  void Load(aether::resources::ResourceManager& resources);

  // Start the loops. Called once, after Load and after the mill's position is
  // known.
  void Begin(aether::audio::AudioSystem& audio, aether::Vec3 mill_at);

  // Per frame: move the listener, and gate each loop on whether its source is
  // doing anything. Gating is by GAIN rather than by stopping and restarting,
  // because a loop restarted every time the mill went idle would click.
  void Update(aether::audio::AudioSystem& audio,
              const aether::audio::Listener& listener,
              const runtime::ViewSnapshot& snapshot, aether::F32 dt);

  [[nodiscard]] bool MillAudible() const { return mill_on_; }
  [[nodiscard]] bool RainAudible() const { return rain_on_; }

 private:
  aether::resources::ResourceHandle<aether::resources::AudioClip> mill_clip_;
  aether::resources::ResourceHandle<aether::resources::AudioClip> rain_clip_;
  aether::audio::VoiceHandle mill_voice_;
  aether::audio::VoiceHandle rain_voice_;
  // The RAMPED gains, which are what actually reach the mixer — the two bools
  // below are only what the snapshot asked for, and the HUD reads those.
  aether::F32 mill_gain_ = 0.0f;
  aether::F32 rain_gain_ = 0.0f;
  bool mill_on_ = false;
  bool rain_on_ = false;
};

}  // namespace hearthfield::view
