#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nameplate_lab {
// One-frame clearance supplied by overhead content, in native UI coordinates.
// The producer includes only content intersecting this arrow's column.
// No actor pointer survives the draw that supplied it.
struct CursorClearance {
    std::uint32_t serverId=0;
    float top=0;
    bool occupied=false;
    void Include(std::uint32_t id,float contentTop) noexcept {
        if(!occupied||serverId!=id){serverId=id;top=contentTop;occupied=true;}
        else top=(std::min)(top,contentTop);
    }
    int Lift(std::uint32_t id,float nativeBottom) const noexcept {
        return occupied&&serverId==id ? static_cast<int>((std::max)(0.0f,std::ceil(nativeBottom-top+2.0f))) : 0;
    }
};
}
