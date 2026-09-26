#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include "layout.h"

namespace nameplate_lab {
// Packet-learned values only. No entity pointers or persistent disk cache.
class Levels {
    static constexpr unsigned Slots = 0x900;
    std::atomic<std::uint64_t> values_[Slots]{};
    struct Request { std::uint32_t id=0, manual=0; bool automatic=false, queued=false, discarded=false; } requests_[Slots];
    // SDK packet callbacks have no same-thread contract. Drawing only loads a
    // packed atomic value; this lock is confined to relevant packet events.
    std::mutex mutex_;
    std::uint64_t nextCheck_=0;
public:
    void Clear();
    void Forget(unsigned index, std::uint32_t id);
    LevelLabel Label(unsigned index, std::uint32_t id) const noexcept;
    bool Prepare(unsigned index, std::uint32_t id, std::uint64_t now);
    bool Outgoing(unsigned index, std::uint32_t id, bool injected, bool blocked);
    bool Result(unsigned index, std::uint32_t id, unsigned level, unsigned difficulty, unsigned message);
};
}
