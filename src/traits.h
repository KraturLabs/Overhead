#pragma once
#include <cstdint>

namespace nameplate_lab {
// Bits 0..6: true sight, sight, sound, magic, job ability, blood, link.
// Aggression is three-state; absence of AggroKnown must never mean passive.
constexpr std::uint16_t TraitIcons = 0x7F, AggroKnown = 0x100, Aggressive = 0x200;
std::uint16_t LookupTraits(unsigned zone, unsigned index, const char* name) noexcept;
}
