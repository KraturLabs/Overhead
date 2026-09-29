#pragma once
#include <cstdint>

namespace overhead {
// XiPackets 0x028: little-endian, unaligned bit fields, starting at byte 5.
struct PacketBits {
    const std::uint8_t* data;unsigned size,pos=40;bool valid=true;
    unsigned Take(unsigned count) noexcept {
        if(pos+count>size*8){valid=false;return 0;}
        unsigned value=0,shift=0;
        while(count){
            const unsigned part=(count<8-(pos&7))?count:8-(pos&7);
            value|=((data[pos/8]>>(pos&7))&((1u<<part)-1))<<shift;
            pos+=part;shift+=part;count-=part;
        }
        return value;
    }
};
}
