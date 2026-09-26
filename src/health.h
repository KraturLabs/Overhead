#pragma once
#include "layout.h"

namespace nameplate_lab {
struct HealthFill { float boundary = 0; bool enabled = false; };
// Called only for damaged monsters. Full health is byte-for-byte original.
HealthFill MeasureHealth(const Output& output, unsigned percent) noexcept;
// One existing glyph becomes at most two clipped quads. No allocations/cache.
unsigned HealthQuads(const Quad& original, const HealthFill& fill, Quad (&pieces)[2]) noexcept;
}
