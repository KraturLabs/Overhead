#pragma once
#include <cstdint>

namespace overhead {
// Bits 0..6: true sight, sight, sound, magic, job ability, blood, link.
// Aggression is three-state; absence of AggroKnown must never mean passive.
constexpr std::uint16_t TraitIcons = 0x7F, AggroKnown = 0x100, Aggressive = 0x200;
// Phoenix damage modifier bits 0..11: slashing, piercing, hand-to-hand, impact, fire, ice,
// wind, earth, lightning, water, light, dark. Weak takes more than normal damage.
constexpr unsigned ModifierCount = 12;
struct MonsterTraits { std::uint16_t bits = 0, weak = 0, resist = 0; };
MonsterTraits LookupMonster(unsigned zone, unsigned index, const char* name) noexcept;
inline std::uint16_t LookupTraits(unsigned zone, unsigned index, const char* name) noexcept {
    return LookupMonster(zone, index, name).bits;
}
}
