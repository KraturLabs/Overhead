#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include "native_signatures.h"

namespace native_discovery {
struct Addresses {
    std::array<std::uint32_t,16> routines{};
    std::uint32_t nameHook=0,nameResume=0,nameExit=0,nameCaller=0;
    std::uint32_t damageHook=0,damageResume=0,damageCaller=0;
    std::uint32_t submit=0,cursorCall=0,menuDraw=0,cursorTail=0;
    std::uint32_t playerIndex=0,entities=0,targets=0,modal=0,config=0;
    std::uint32_t expansionBase=0,expansionCount=0,font=0,graphics=0,targetWindow=0;
};
inline bool Matches(const unsigned char* data,const Pattern& p) noexcept {
    for(unsigned i=0;i<p.size;++i)if(p.mask[i]&&data[i]!=p.bytes[i])return false;
    return true;
}
// One setup pass. The caller provides a readable snapshot of the loaded image.
// Return a unique structural match, never the first of several candidates.
inline bool Discover(const unsigned char* image,unsigned size,std::uint32_t base,Addresses& result) noexcept {
    Addresses found;
    for(unsigned i=0;i<found.routines.size();++i){
        const auto& p=patterns[i];
        if(size<p.size)return false;
        const auto* begin=image+p.anchor;
        const auto* end=image+size-(p.size-p.anchor-p.length);
        unsigned matches=0;
        while(begin<end){
            const auto* hit=std::search(begin,end,p.bytes+p.anchor,p.bytes+p.anchor+p.length);
            if(hit==end)break;
            const auto* candidate=hit-p.anchor;
            if(Matches(candidate,p)){
                if(++matches>1)return false;
                found.routines[i]=static_cast<unsigned>(candidate-image);
            }
            begin=hit+1;
        }
        if(matches!=1)return false;
    }
    const auto& r=found.routines;
    const auto absolute=[&](unsigned routine,unsigned offset,unsigned adjust=0){
        std::uint32_t value;std::memcpy(&value,image+r[routine]+offset,4);
        return value-base+adjust;
    };
    found.nameHook=r[0]+0x1D1;found.nameResume=found.nameHook+6;found.nameExit=r[0]+0x6D7;
    found.nameCaller=r[1]+0x28;
    found.damageHook=r[4];found.damageResume=r[4]+6;found.damageCaller=r[3]+0x11F;
    found.submit=r[2];found.cursorCall=r[6]+0x41;found.menuDraw=r[7];found.cursorTail=r[9]+0x316;
    found.playerIndex=absolute(13,1,4);found.entities=absolute(14,0xC);
    found.targets=absolute(8,0x12);found.modal=absolute(7,0x1F);found.config=absolute(13,0x44);
    found.expansionBase=absolute(0,0x1F9,200);found.expansionCount=absolute(0,0x1FF,200);
    found.font=absolute(0,0x29D);found.graphics=absolute(0,0x60);found.targetWindow=absolute(15,2);
    // Derived globals must belong to this image before they are ever dereferenced.
    for(auto value:{found.playerIndex,found.entities,found.targets,found.modal,found.config,
                   found.expansionBase,found.expansionCount,found.font,found.graphics,found.targetWindow})
        if(value>=size||size-value<16)return false;
    const auto calls=[&](unsigned at,unsigned target){
        std::int32_t displacement;std::memcpy(&displacement,image+at+1,4);
        return image[at]==0xE8&&at+5+displacement==target;
    };
    if(!calls(found.cursorCall,found.menuDraw)||!calls(found.nameCaller-5,r[0])
        ||!calls(found.damageCaller-5,r[4]))return false;
    result=found;return true;
}
}
