#include "levels.h"
#include <cstring>

namespace nameplate_lab {
void Levels::Clear() {
    const std::lock_guard lock(mutex_);
    for(unsigned i=0;i<Slots;++i){values_[i].store(0,std::memory_order_relaxed);scanned_[i].store(0,std::memory_order_relaxed);requests_[i]={};}
    nextCheck_=0;
}
void Levels::Forget(unsigned index,std::uint32_t id) {
    if(index>=Slots)return;
    const std::lock_guard lock(mutex_);
    scanned_[index].store(0,std::memory_order_relaxed); // The index may be reused.
    if(static_cast<std::uint32_t>(values_[index].load(std::memory_order_relaxed))==id)
        values_[index].store(0,std::memory_order_relaxed);
    // Preserve attribution for an in-flight reply even after the mob disappears:
    // consume it silently, but do not attach its level to a respawn.
    if(requests_[index].id==id)requests_[index].discarded=true;
}
LevelLabel Levels::Label(unsigned index,std::uint32_t id) const noexcept {
    LevelLabel label;
    if(index>=Slots||!id)return label;
    const auto value=values_[index].load(std::memory_order_relaxed);
    auto level=static_cast<std::uint32_t>(value)==id?static_cast<unsigned>((value>>32)&0xFFFF):0u;
    // Too weak, incredibly easy prey, easy prey, decent, even, tough, very tough, incredibly tough.
    constexpr std::uint32_t colors[]={0xA0A0A0,0x40E040,0x40D0E0,0x6080FF,0xFFFF40,0xFFA040,0xFF4040,0xFF4040};
    constexpr const char* checks[]={"TW","IEP","EP","DC","EM","T","VT","IT"};
    if(level==256){label.color=0xFF40FF;label.checkLength=2;std::memcpy(label.check,"NM",2);}
    else if(level){
        const auto rank=static_cast<unsigned>(value>>48)-64;
        label.color=colors[rank];
        label.checkLength=static_cast<unsigned>(std::strlen(checks[rank]));
        std::memcpy(label.check,checks[rank],label.checkLength);
    }
    // A widescan level without a check reply has no difficulty: neutral white.
    else if((level=scanned_[index].load(std::memory_order_relaxed))!=0)label.color=0xFFFFFF;
    else return label;
    if(level==256){label.length=6;std::memcpy(label.text,"Lv.???",6);}
    else {
        std::memcpy(label.text,"Lv.",3);label.length=3;
        if(level>=100)label.text[label.length++]=static_cast<char>('0'+level/100);
        if(level>=10)label.text[label.length++]=static_cast<char>('0'+(level/10)%10);
        label.text[label.length++]=static_cast<char>('0'+level%10);
    }
    return label;
}
bool Levels::Prepare(unsigned index,std::uint32_t id,std::uint64_t now) {
    if(index>=Slots||!id)return false;
    const std::lock_guard lock(mutex_);
    auto& request=requests_[index];
    if(request.id!=id||(!request.automatic&&!request.manual&&request.discarded))request={id};
    if(now<nextCheck_||request.manual||request.automatic
        ||static_cast<std::uint32_t>(values_[index].load(std::memory_order_relaxed))==id)return false;
    request.automatic=true;request.queued=true;
    values_[index].store(id,std::memory_order_relaxed); // Attempt once, including no-response targets.
    nextCheck_=now+1000; // Bound requests when cycling targets; no retries or timer.
    return true;
}
bool Levels::Outgoing(unsigned index,std::uint32_t id,bool injected,bool blocked) {
    if(index>=Slots||!id)return false;
    const std::lock_guard lock(mutex_);
    auto& request=requests_[index];
    if(request.id!=id)request={id};
    if(injected&&request.queued){
        request.queued=false;
        // A manual check overtook our queued injection. Cancel ours before it
        // reaches the server; that manual reply must remain visible.
        if(blocked||request.manual||request.discarded||(values_[index].load(std::memory_order_relaxed)>>32)){
            request.automatic=false;return !blocked;
        }
    }else if(!blocked){if(!request.automatic&&!request.manual)request.discarded=false;++request.manual;}
    return false;
}
void Levels::Scanned(unsigned index,int level) noexcept {
    if(index<Slots&&level>0&&level<=255)scanned_[index].store(static_cast<std::uint8_t>(level),std::memory_order_relaxed);
}
bool Levels::Result(unsigned index,std::uint32_t id,unsigned level,unsigned difficulty,unsigned message) {
    const bool impossible=message==249;
    if(index>=Slots||!id||(!impossible&&(message<170||message>178)))return false;
    const std::lock_guard lock(mutex_);
    auto& request=requests_[index];
    if((request.id!=id||!request.discarded)&&(impossible||(level>0&&level<=255&&difficulty>=64&&difficulty<=71)))
        values_[index].store(id|(std::uint64_t(impossible?256:level)<<32)
            |(std::uint64_t(difficulty)<<48),std::memory_order_relaxed);
    if(request.id!=id)return false;
    // The protocol has no request token. Replies for one target follow outgoing
    // request order: our sent request precedes any subsequent manual requests.
    if(request.automatic&&!request.queued){request.automatic=false;return true;}
    if(request.manual)--request.manual;
    return false;
}
}
