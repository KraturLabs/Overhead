#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
#include "layout.h"

namespace nameplate_lab {
// Status IDs from client resources. Only negative effects belong in this row.
inline constexpr std::uint16_t DebuffIds[]={
    2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,28,29,30,31,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,144,145,146,147,148,149,
    156,167,168,174,175,186,189,192,193,194,291,298,378,379,380,
    386,387,388,389,390,391,392,393,394,395,396,397,398,399,400,404,
    448,449,450,451,452,536,557,558,559,560,561,562,563,564,571,572
};
inline constexpr auto DebuffCells=[](){
    std::array<unsigned,640> cells{};cells.fill(~0u);
    for(unsigned i=0;i<sizeof(DebuffIds)/sizeof(*DebuffIds);++i)cells[DebuffIds[i]]=i;
    return cells;
}();
constexpr unsigned DebuffCell(unsigned effect) noexcept {
    return effect<DebuffCells.size()?DebuffCells[effect]:~0u;
}
enum class DebuffChange { Add, Remove, Damage, Defeat };
struct DebuffEvent { std::uint32_t target; DebuffChange change; unsigned effect; unsigned rank=0; };
using DebuffSink=void(*)(void*,const DebuffEvent&);
// Decode only combat results, not cast/readies. Never modify or block packets.
void DecodeDebuffs(unsigned packet,const std::uint8_t* data,unsigned size,void* context,DebuffSink sink) noexcept;

class Debuffs {
    struct Row {
        std::atomic<std::uint32_t> id{0};
        // One coherent effect + expiry per load. No native pointers or draw lock.
        std::atomic<std::uint64_t> effects[MaxDebuffs]{};
    } rows_[0x900];
    std::mutex mutex_; // Relevant packet writers only; SDK has no serialization contract.
public:
    void Clear();
    void Forget(unsigned index,std::uint32_t id);
    void Apply(unsigned index,std::uint32_t id,DebuffChange change,unsigned effect,std::uint32_t now,unsigned rank=0);
    DebuffRow Read(unsigned index,std::uint32_t id,std::uint32_t now) const noexcept;
};
}
