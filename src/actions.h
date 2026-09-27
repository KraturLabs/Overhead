#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include "layout.h"

namespace nameplate_lab {
enum class ActionChange { Start, Instant, Finish, Clear };
enum ActionResult : unsigned { ActionNeutral, ActionSuccess, ActionFailure };
// Half-intensity soft green / soft red / white under native doubled modulation.
constexpr std::uint32_t ActionColor(unsigned result) noexcept {
    return result==ActionSuccess?0x4D6B53u:result==ActionFailure?0x734B4Bu:0x808080u;
}
// Start: category 7/8 ready or cast with its ID. Instant: player job ability
// (category 6/14/15), ID from the header. Finish/Clear name no action.
struct ActionEvent { std::uint32_t actor; ActionChange change; unsigned category=0, id=0, result=ActionNeutral; };
using ActionSink=void(*)(void*,const ActionEvent&);
// Decode only action starts/results. Never modify or block packets.
void DecodeActions(unsigned packet,const std::uint8_t* data,unsigned size,void* context,ActionSink sink) noexcept;

class Actions {
    struct Row {
        std::atomic<std::uint32_t> id{0};
        // Expiry, finished flag, result and length in one load; text follows.
        std::atomic<std::uint64_t> state{0};
        std::atomic<std::uint64_t> text[(MaxLabel+1)/8]{};
    } rows_[0x900];
    std::mutex mutex_; // Relevant packet writers only; drawing uses atomic loads.
public:
    void Clear();
    void Forget(unsigned index,std::uint32_t id);
    // Shows a name until finished or the lost-result timeout.
    void Show(unsigned index,std::uint32_t id,const char* name,unsigned result,bool finished,std::uint32_t now);
    // The first result starts the linger; duplicate results do not extend it.
    void Finish(unsigned index,std::uint32_t id,unsigned result,std::uint32_t now);
    TextLabel Read(unsigned index,std::uint32_t id,std::uint32_t now) const noexcept;
};
}
