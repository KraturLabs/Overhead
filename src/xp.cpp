#include "xp.h"
#include <utility>

namespace nameplate_lab {
namespace {
constexpr std::uint32_t Colors[]={0,0x807040u,0x5A7080u,0x806080u,0x607850u}; // Half intensity.
constexpr const char* Units[]={""," XP"," LP"," CP"," EP"};
void append(TextLabel& label,const char* text) noexcept {
    while(*text&&label.length<MaxFloatText)label.text[label.length++]=*text++;
}
void append(TextLabel& label,unsigned value) noexcept {
    char digits[10];unsigned count=0;
    do{digits[count++]=static_cast<char>('0'+value%10);value/=10;}while(value&&count<10);
    while(count&&label.length<MaxFloatText)label.text[label.length++]=digits[--count];
}
}
unsigned XpMessageKind(unsigned message,bool& chain) noexcept {
    chain=message==253||message==372||message==735||message==810;
    switch(message){
    case 8:case 253:return XpExperience;
    case 371:case 372:return XpLimit;
    case 718:case 735:return XpCapacity;
    case 809:case 810:return XpExemplar;
    default:return 0;
    }
}
void XpFeed::Add(unsigned kind,unsigned amount,unsigned chain,std::uint32_t nowMs) noexcept {
    if(!kind||kind>XpExemplar||!amount)return;
    if(amount>0xFFFFF)amount=0xFFFFF;
    if(chain>255)chain=255;
    slots_[next_].store((std::uint64_t(nowMs|1)<<32)|(amount<<12)|(chain<<4)|kind,std::memory_order_release);
    next_=(next_+1)%MaxFloating;
}
void XpFeed::Read(FloatingLabel (&out)[MaxFloating],std::uint32_t nowMs) const noexcept {
    std::uint64_t live[MaxFloating];unsigned count=0;
    for(auto& slot:slots_){
        const auto value=slot.load(std::memory_order_acquire);
        if(!value)continue;
        const auto age=nowMs-static_cast<std::uint32_t>(value>>32);
        if(age<XpLifeMs)live[count++]=value;
    }
    // Newest (smallest age) first.
    for(unsigned i=1;i<count;++i)
        for(unsigned j=i;j&&static_cast<std::uint32_t>(nowMs-(live[j]>>32))<static_cast<std::uint32_t>(nowMs-(live[j-1]>>32));--j)
            std::swap(live[j],live[j-1]);
    float below=0;
    for(unsigned i=0;i<MaxFloating;++i){
        out[i]={};
        if(i>=count)continue;
        const auto value=live[i];
        const auto kind=static_cast<unsigned>(value&15),chain=static_cast<unsigned>((value>>4)&255);
        const float t=static_cast<float>(static_cast<std::uint32_t>(nowMs-(value>>32)))/XpLifeMs;
        auto& label=out[i].text;
        if(chain){append(label,"Chain ");append(label,chain);append(label," ");}
        append(label,static_cast<unsigned>((value>>12)&0xFFFFF));append(label,Units[kind]);
        label.color=Colors[kind];
        // Two name heights of drift over the life; fully visible for the first
        // third, then a slow fade to nothing.
        out[i].drop=below>2*t?below:2*t;below=out[i].drop+1.1f;
        out[i].alpha=t<1.f/3?1:1.5f*(1-t);
    }
}
}
