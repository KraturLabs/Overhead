#pragma once
#include <cstdint>
// Supported client entry and continuation RVAs; validated before hook installation.
namespace native_contract {
inline constexpr std::uint32_t NameHook=0x863E1,NameResume=0x863E7,NameExit=0x868E7;
inline constexpr std::uint32_t CursorCall=0x15F729,MenuDraw=0x1177D0;
inline constexpr std::uint32_t DamageHook=0x41C20,DamageResume=0x41C26;
}
