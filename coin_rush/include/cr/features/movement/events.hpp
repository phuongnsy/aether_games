// Events movement emits (Jumped/Landed/WallHit). Payloads live in the shared
// bus vocabulary (cr/runtime/events.hpp) so runtime never depends on a feature.
#pragma once

#include "cr/runtime/events.hpp"  // Jumped, Landed, WallHit, GameEvent, EventList
