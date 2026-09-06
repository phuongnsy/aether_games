// The coins feature emits CoinCollected. The payload lives in the shared bus
// vocabulary (cr/runtime/events.hpp) so runtime never depends on a feature.
#pragma once

#include "cr/runtime/events.hpp"  // CoinCollected, GameEvent, EventList
