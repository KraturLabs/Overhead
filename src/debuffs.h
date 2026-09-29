#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include "layout.h"

namespace overhead {
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
// ifAbsent: the server says the effect is already on ("no effect"); add it only if untracked.
struct DebuffEvent { std::uint32_t target; DebuffChange change; unsigned effect; unsigned rank=0; unsigned duration=300; bool preserveLonger=false; bool ifAbsent=false; };
using DebuffSink=void(*)(void*,const DebuffEvent&);
// Decode only combat results, not cast/readies. Never modify or block packets.
void DecodeDebuffs(unsigned packet,const std::uint8_t* data,unsigned size,void* context,DebuffSink sink) noexcept;

class Debuffs {
    struct Row {
        std::atomic<std::uint32_t> id{0};
        // One coherent effect + expiry per load. No native pointers or draw lock.
        std::atomic<std::uint64_t> effects[MaxDebuffs]{};
        // Movement watch (after Bars): a hold-type effect arms it 0.5 s after landing;
        // the first draw after that records where the target stood and faced.
        std::atomic<std::uint32_t> watchFrom{0}, anchoredFor{0}; // Tick ms; anchored when equal.
        std::atomic<float> anchorX{0}, anchorY{0}, anchorHeading{0};
    } rows_[0x900];
    // Relevant packet writers only (always exclusive); SDK has no serialization contract.
    // Zero-initialized SRW lock keeps the table out of the DLL file, unlike std::mutex.
    std::shared_mutex mutex_;
public:
    void Clear();
    void Forget(unsigned index,std::uint32_t id);
    void Apply(unsigned index,std::uint32_t id,DebuffChange change,unsigned effect,std::uint32_t now,unsigned rank=0,unsigned duration=300,
        bool preserveLonger=false,bool ifAbsent=false,std::uint32_t millis=0);
    // Draw thread, lock-free: moving over 3 yalms on either axis from the recorded
    // spot ends Sleep, Petrify, Stun and Bind; turning over 10 degrees ends Sleep and Petrify.
    void Watch(unsigned index,std::uint32_t id,std::uint32_t millis,float x,float y,float heading) noexcept;
    DebuffRow Read(unsigned index,std::uint32_t id,std::uint32_t now) const noexcept;
};
}
