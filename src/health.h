#pragma once
#include "layout.h"

namespace nameplate_lab {
// tint: RGB for the remaining (undepleted) letters; 0 keeps the native color.
struct HealthFill { float boundary = 0; bool enabled = false; std::uint32_t tint = 0; };
// FFXI HP warning bands, softened: white, then light yellow below 75%, light
// orange below 50%, light red below 25%. Half intensity under doubled modulation.
constexpr std::uint32_t HealthColor(unsigned percent) noexcept {
    return percent<25?0x804848u:percent<50?0x806040u:percent<75?0x808048u:0x808080u;
}
// Native font group 0 holds letters/punctuation; group 1 holds status icons.
// Spaces affect the gap between letters but never extend the outer bounds.
// Only these quads take the HP fill; decorations (0x100|code) and icons do not.
inline bool HealthLetter(const Quad& q) noexcept {
    return q.textureGroup == 0 && (q.code & ~FrontCode) > 32 && (q.code & ~FrontCode) < 142;
}
// Called only for damaged monsters (and optionally your party). Full health is byte-for-byte original.
HealthFill MeasureHealth(const Output& output, unsigned percent) noexcept;
// One existing glyph becomes at most two clipped quads. No allocations/cache.
unsigned HealthQuads(const Quad& original, const HealthFill& fill, Quad (&pieces)[2]) noexcept;
}
