#pragma once
#include <atomic>
#include <cstdint>
#include "layout.h"

namespace nameplate_lab {
// Points you gain (0x02D, sender and target both you), shown drifting down
// below your own name. One packet writer; drawing uses atomic loads.
enum XpKind : unsigned { XpExperience=1, XpLimit, XpCapacity, XpExemplar };
constexpr std::uint32_t XpLifeMs=3000;
// Message IDs (LandSandBoat xi.msg.basic): plain gain, then chain.
unsigned XpMessageKind(unsigned message,bool& chain) noexcept;
class XpFeed {
    // Start ms (32) | amount (20) | chain (8) | kind (4); zero is empty.
    std::atomic<std::uint64_t> slots_[MaxFloating]{};
    unsigned next_=0;
public:
    void Clear() noexcept {for(auto& slot:slots_)slot.store(0,std::memory_order_relaxed);}
    void Add(unsigned kind,unsigned amount,unsigned chain,std::uint32_t nowMs) noexcept;
    // Newest first; each keeps its own drift, pushed down below newer ones.
    void Read(FloatingLabel (&out)[MaxFloating],std::uint32_t nowMs) const noexcept;
};
}
