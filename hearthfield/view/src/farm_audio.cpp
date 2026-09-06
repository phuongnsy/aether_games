#include "hf/view/farm_audio.hpp"

#include <algorithm>
#include <string>

#include "aether/core/log.hpp"

namespace hearthfield::view {

using namespace aether;

namespace {

// Loud enough to notice, quiet enough to leave on. The mill's own gain, before
// distance does anything to it.
constexpr F32 kMillGain = 0.55f;
// 0.30 UNTIL 2026-08-29, and the number had to come down because the CLIP
// changed under it: the old rain.wav was 73% digital silence, so its average
// energy was about a seventh of what a continuous bed at the same gain puts
// into the mix. Rain is the one sound here that plays for minutes at a time
// with nothing to interrupt it, so it sits well under the mill rather than
// beside it.
constexpr F32 kRainGain = 0.10f;

// Gain per second while a loop fades in or out. RAIN IS THE SLOW ONE ON
// PURPOSE: a shower arrives, and snapping an ambience to full is the artefact
// that makes weather read as a switch. A mill is a machine and may start like
// one.
constexpr F32 kRainFadePerSecond = 0.40f;
constexpr F32 kMillFadePerSecond = 2.50f;

F32 Approach(F32 value, F32 target, F32 rate, F32 dt) {
  const F32 step = rate * std::max(dt, 0.0f);
  return value < target ? std::min(target, value + step)
                        : std::max(target, value - step);
}

}  // namespace

audio::Listener ListenerFor(Vec3 target, F32 yaw, F32 pitch) {
  // THE POSITION IS THE TARGET, NOT THE EYE. Under a parallel projection the
  // eye's distance is arbitrary — ADR-0081 — so measuring from it would make
  // every source on the farm the same distance away.
  //
  // The ORIENTATION is the camera's, taken the way OrbitComponent builds it:
  // yaw about +Y, then pitch about the rotated +X. Copied deliberately rather
  // than read off the node, because the listener has to face where the camera
  // faces and an independent derivation is what would silently drift.
  return audio::Listener{
      .position = target,
      .orientation = QuatFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yaw) *
                     QuatFromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, -pitch)};
}

void FarmAudio::Load(resources::ResourceManager& resources) {
  if (auto clip = resources.Load<resources::AudioClip>("audio/mill.wav")) {
    mill_clip_ = *clip;
  } else {
    LogWarn("hearthfield: no mill sound ({}) — the mill will be silent",
            clip.error().message);
  }
  if (auto clip = resources.Load<resources::AudioClip>("audio/rain.wav")) {
    rain_clip_ = *clip;
  } else {
    LogWarn("hearthfield: no rain sound ({})", clip.error().message);
  }
}

void FarmAudio::Begin(audio::AudioSystem& audio, Vec3 mill_at) {
  if (mill_clip_) {
    // 3D: check 8's other half. Started at gain 0 and left running — see
    // Update on why this is not stopped and restarted.
    mill_voice_ =
        audio.PlaySpatial(mill_clip_,
                          audio::SpatialSource{.position = mill_at,
                                               .falloff_min = kMillFalloffMin,
                                               .falloff_max = kMillFalloffMax,
                                               .gain = 0.0f},
                          /*loop=*/true);
  }
  if (rain_clip_) {
    // 2D: GEA §14.4.1 puts rainfall here. Played flat — no position, no pan,
    // because rain arrives from everywhere and spatialising it would put the
    // whole sky in one speaker.
    rain_voice_ = audio.Play(
        rain_clip_, audio::PlayParams{.gain = 0.0f, .pan = 0.0f, .loop = true});
  }
}

void FarmAudio::Update(audio::AudioSystem& audio,
                       const audio::Listener& listener,
                       const runtime::ViewSnapshot& snapshot, F32 dt) {
  audio.SetListener(listener);

  // GATED BY GAIN, NOT BY Stop/Play. A loop restarted every time the mill went
  // idle would click on every transition and would restart the sample from its
  // attack, which for a 2 s rumble is audible as a thump. Zero gain keeps the
  // voice alive and its position tracked.
  //
  // RAMPED, not assigned. `raining` is a step function of the tick, so writing
  // it straight to the gain makes a shower begin and end between two frames —
  // which reads as a switch being thrown rather than as weather. THE RENDER dt,
  // like the drops themselves: this is presentation and belongs nowhere near
  // the world clock.
  mill_on_ = snapshot.milling > 0;
  rain_on_ = snapshot.raining;
  mill_gain_ =
      Approach(mill_gain_, mill_on_ ? kMillGain : 0.0f, kMillFadePerSecond, dt);
  rain_gain_ =
      Approach(rain_gain_, rain_on_ ? kRainGain : 0.0f, kRainFadePerSecond, dt);
  if (mill_voice_.Valid()) {
    audio.SetSpatialGain(mill_voice_, mill_gain_);
  }
  if (rain_voice_.Valid()) {
    audio.SetMix(rain_voice_, rain_gain_, 0.0f);
  }
}

}  // namespace hearthfield::view
