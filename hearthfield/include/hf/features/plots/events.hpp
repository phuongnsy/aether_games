// The plots feature emits Sown, CropReady and Harvested. The payloads live in
// the shared bus vocabulary (hf/runtime/events.hpp) so runtime never depends
// on a feature.
#pragma once

#include "hf/runtime/events.hpp"  // Sown, CropReady, Harvested, GameEvent
