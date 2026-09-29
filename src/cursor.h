#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace overhead {
// One-frame clearance supplied by overhead content, in render-buffer pixels.
// The producer includes only content intersecting this arrow's column.
// No actor pointer survives the draw that supplied it.
struct CursorClearance {
    std::uint32_t serverId=0;
    float top=0;
    bool occupied=false;
    float left=0,right=0;
    bool bounded=false;
    void Include(std::uint32_t id,float contentTop) noexcept {
        if(!occupied||serverId!=id){serverId=id;top=contentTop;occupied=true;}
        else top=(std::min)(top,contentTop);
    }
    void IncludeBounds(std::uint32_t id,float contentTop,float contentLeft,float contentRight) noexcept {
        Include(id,contentTop);left=contentLeft;right=contentRight;bounded=true;
    }
    int Lift(std::uint32_t id,float nativeBottom,float nativeX=0,float uiScaleX=1,float uiScaleY=1) const noexcept {
        return occupied&&serverId==id&&(!bounded||(nativeX>=left*uiScaleX&&nativeX<=right*uiScaleX))
            ? static_cast<int>((std::max)(0.0f,std::ceil(nativeBottom-top*uiScaleY+2.0f))) : 0;
    }
};
}
