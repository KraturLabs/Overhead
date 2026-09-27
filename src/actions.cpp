#include "actions.h"
#include "packet_bits.h"
#include <cstring>

namespace nameplate_lab {
namespace {
constexpr std::uint32_t LostResultSeconds=30,LingerSeconds=6;
// Primary result messages (LandSandBoat xi.msg.basic). Anything else stays neutral.
constexpr std::uint16_t Failures[]={4,5,15,16,17,18,29,30,31,34,35,36,47,48,49,70,71,72,
    75,76,78,84,85,87,88,106,114,137,153,154,155,156,158,188,189,190,192,283,323,324,655};
constexpr std::uint16_t Successes[]={2,7,83,93,100,101,102,103,108,109,110,119,123,125,126,
    127,129,136,142,144,146,147,149,150,151,159,185,186,187,203,205,230,236,237,238,242,
    252,264,265,266,267,268,269,271,272,277,278,279,280,317,319,320,375,412,645,754,755,802,804};
template<std::size_t N> bool listed(const std::uint16_t (&list)[N],unsigned message) noexcept {
    for(auto value:list)if(value==message)return true;
    return false;
}
bool interrupted(unsigned message) noexcept {return message==16||message==84||message==106;}
bool defeated(unsigned message) noexcept {
    return message==6||message==20||message==97||message==113||message==406||message==605||message==646;
}
unsigned u32(const std::uint8_t* p) noexcept {
    return p[0]|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
}
bool alive(std::uint64_t state,std::uint32_t now) noexcept {
    return state&&static_cast<std::int32_t>(static_cast<std::uint32_t>(state>>32)-now)>0;
}
std::uint64_t pack(std::uint32_t expiry,unsigned result,bool finished,unsigned length) noexcept {
    return (std::uint64_t(expiry)<<32)|(finished?0x400u:0u)|(result<<8)|length;
}
}
void DecodeActions(unsigned packet,const std::uint8_t* data,unsigned size,void* context,ActionSink sink) noexcept {
    if(packet==0x029){
        if(size<0x1C)return;
        const auto message=(data[24]|(unsigned(data[25])<<8))&0x7FFF;
        if(interrupted(message))sink(context,{u32(data+4),ActionChange::Finish,0,0,ActionFailure});
        else if(defeated(message))sink(context,{u32(data+8),ActionChange::Clear});
        return;
    }
    if(packet!=0x028||size<19)return;
    PacketBits bits{data,size};
    const auto actor=bits.Take(32),targets=bits.Take(6);
    bits.Take(4);const auto category=bits.Take(4),argument=bits.Take(32);bits.Take(32);
    const bool instant=category==6||category==14||category==15;
    if(!targets||!(instant||category==3||category==4||category==7||category==8||category==11))return;
    unsigned id=0,first=0;bool succeeded=false,failed=false,unknown=false;
    for(unsigned t=0;t<targets;++t){
        bits.Take(32);const auto actions=bits.Take(4);
        if(!actions||actions>8)return; // Same maximum as the client; reject truncated/invalid records.
        for(unsigned a=0;a<actions;++a){
            bits.Take(27);const auto value=bits.Take(17),message=bits.Take(10);bits.Take(31);
            if(bits.Take(1))bits.Take(37);
            if(bits.Take(1))bits.Take(34);
            if(!bits.valid)return;
            if(!t&&!a){id=value;first=message;}
            if(listed(Successes,message))succeeded=true;
            else if(listed(Failures,message))failed=true;
            else unknown=true;
        }
    }
    const unsigned result=succeeded?ActionSuccess:failed&&!unknown?ActionFailure:ActionNeutral;
    // Instant abilities have no ready phase and carry their ID in the header.
    if(instant){if(actor&&actor<0x1000000)sink(context,{actor,ActionChange::Instant,category,argument,result});return;}
    if(category==3||category==4||category==11){sink(context,{actor,ActionChange::Finish,category,0,result});return;}
    // Ready/cast header: the low 16 bits mark a start or an interruption.
    if((argument&0xFFFF)==0x7073||interrupted(first))sink(context,{actor,ActionChange::Finish,category,0,ActionFailure});
    else if(!first)sink(context,{actor,ActionChange::Finish,category,0,ActionNeutral});
    else sink(context,{actor,ActionChange::Start,category,id});
}
void Actions::Clear(){
    const std::lock_guard lock(mutex_);
    for(auto& row:rows_)row.id.store(0,std::memory_order_release);
}
void Actions::Forget(unsigned index,std::uint32_t id){
    if(index>=0x900)return;
    const std::lock_guard lock(mutex_);
    if(rows_[index].id.load()==id)rows_[index].id.store(0,std::memory_order_release);
}
void Actions::Show(unsigned index,std::uint32_t id,const char* name,unsigned result,bool finished,std::uint32_t now){
    if(index>=0x900||!id)return;
    const std::lock_guard lock(mutex_);
    auto& row=rows_[index];
    row.id.store(0,std::memory_order_release);
    unsigned length=0;
    while(name&&length<MaxLabel&&name[length]){
        // Bytes past ASCII address native icon glyphs, not characters.
        if(name[length]<32||name[length]>126)return;
        ++length;
    }
    if(!length)return; // An unresolved name must not leave the preceding action on screen.
    char text[MaxLabel+1]{};
    std::memcpy(text,name,length);
    for(unsigned i=0;i<(MaxLabel+1)/8;++i){
        std::uint64_t word;std::memcpy(&word,text+i*8,8);
        row.text[i].store(word,std::memory_order_relaxed);
    }
    row.state.store(pack(now+(finished?LingerSeconds:LostResultSeconds),result,finished,length),std::memory_order_relaxed);
    row.id.store(id,std::memory_order_release);
}
void Actions::Finish(unsigned index,std::uint32_t id,unsigned result,std::uint32_t now){
    if(index>=0x900||!id)return;
    const std::lock_guard lock(mutex_);
    auto& row=rows_[index];
    const auto state=row.state.load(std::memory_order_relaxed);
    if(row.id.load()!=id||(state&0x400)||!alive(state,now))return;
    row.state.store(pack(now+LingerSeconds,result,true,static_cast<unsigned>(state&0x3F)),std::memory_order_relaxed);
}
TextLabel Actions::Read(unsigned index,std::uint32_t id,std::uint32_t now) const noexcept {
    TextLabel label;
    if(index>=0x900||!id)return label;
    const auto& row=rows_[index];
    if(row.id.load(std::memory_order_acquire)!=id)return label;
    const auto state=row.state.load(std::memory_order_relaxed);
    if(!alive(state,now))return label;
    for(unsigned i=0;i<(MaxLabel+1)/8;++i){
        const auto word=row.text[i].load(std::memory_order_relaxed);
        std::memcpy(label.text+i*8,&word,8);
    }
    if(row.id.load(std::memory_order_acquire)!=id)return {};
    label.length=static_cast<unsigned>(state&0x3F);
    label.color=ActionColor((state>>8)&3);
    return label;
}
}
