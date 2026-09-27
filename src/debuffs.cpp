#include "debuffs.h"
#include "debuff_rules.h"

namespace nameplate_lab {
namespace {
bool alive(std::uint64_t value,std::uint32_t now) noexcept {
    return value&&static_cast<std::int32_t>(static_cast<std::uint32_t>(value>>32)-now)>0;
}
unsigned family(unsigned effect) noexcept {
    if(effect==2||effect==19||effect==193)return 2;
    if(effect>=386&&effect<=400)return 386+(effect-386)/5*5;
    if(effect>=448&&effect<=452)return 448;
    return effect;
}
// XiPackets 0x028: little-endian, unaligned bit fields, starting at byte 5.
struct Bits {
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
unsigned procDuration(unsigned effect) noexcept {
    switch(effect){
    case 2:case 19:case 193:return 25;
    case 3:case 130:return 90;
    case 4:case 6:case 140:case 146:case 147:case 148:case 167:case 175:return 120;
    case 5:case 13:return 180;
    case 10:return 5;
    case 11:case 12:case 16:case 571:return 30;
    case 28:case 378:case 379:case 380:return 10;
    case 31:case 136:case 149:case 298:case 404:case 448:case 536:return 60;
    case 156:return 12;
    case 168:return 100;
    default:return 30;
    }
}
DebuffSpell actionRule(unsigned category,unsigned action) noexcept {
    if(category==4)return action<DebuffSpells.size()?DebuffSpells[action]:DebuffSpell{};
    if(category!=6&&category!=14)return {};
    if(action>=512)action-=512;
    switch(action){
    case 57:return {11,30}; // Shadowbind
    case 131:return {2,90}; // Light Shot
    case 170:return {149,30}; // Angon
    case 205:return {12,30}; // Desperate Flourish
    case 207:return {10,2}; // Violent Flourish
    case 161:return {28,10}; // Feral Howl
    case 372:return {536,60}; // Gambit
    case 375:return {571,30}; // Rayke
    default:return {};
    }
}
void message(void* context,DebuffSink sink,std::uint32_t target,unsigned msg,unsigned value,unsigned spell=0,bool additional=false,unsigned category=4) noexcept {
    unsigned duration=0;
    DebuffChange change;
    switch(msg){
    case 127:case 160:case 164:case 186:case 203:case 205:case 230:case 236:case 237:case 242:case 243:
    case 194:case 269:case 272:case 375:case 412:case 645:case 754:case 755:case 804:
    case 266:case 267:case 268:case 271:case 277:case 278:case 279:case 280:case 319:case 320:case 374:
        {
            const auto rule=actionRule(category,spell);
            if(!value)value=rule.effect;
            if((rule.effect==19||rule.effect==193)&&value==2)value=rule.effect;
            const bool secondary=category==4&&((spell==565&&value==6)
                ||(spell==604&&(value==6||value==4||value==11||value==12||value==3||value==5))
                ||(spell==633&&value==167)||(spell==634&&value==11)
                ||(spell==651&&value==147)||(spell==671&&value==11));
            if(rule.seconds&&(value==rule.effect||secondary))duration=rule.seconds;
        }
        if(DebuffCell(value)==~0u)return;
        if(!duration)duration=additional?procDuration(value)
            :DebuffStatusSeconds[value]?DebuffStatusSeconds[value]:procDuration(value);
        change=DebuffChange::Add;break;
    case 519:case 520:case 521:case 591:
        if(!value||value>10)return;
        duration=value==1?60:value==2?90:120;
        value=(msg==591?448:386+(msg-519)*5)+(value>5?4:value-1);
        change=DebuffChange::Add;break;
    case 64:case 83:case 123:case 159:case 168:case 204:case 206:case 321:case 322:
    case 341:case 342:case 343:case 344:case 350:case 378:case 453:
    case 531:case 647:case 805:case 806:
        change=DebuffChange::Remove;break;
    case 1:case 2:case 67:case 77:case 110:case 157:case 161:case 163:case 185:
    case 187:case 197:case 227:case 229:case 252:case 264:case 317:case 352:case 353:
    case 265:case 379:case 802:case 576:case 577:
        if(!value)return;
        change=DebuffChange::Damage;break;
    case 6:case 20:case 97:case 113:case 406:case 605:case 646:
        change=DebuffChange::Defeat;break;
    default:return;
    }
    sink(context,{target,change,value,0,duration,additional});
}
unsigned u32(const std::uint8_t* p) noexcept {
    return p[0]|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
}
}
void DecodeDebuffs(unsigned packet,const std::uint8_t* data,unsigned size,void* context,DebuffSink sink) noexcept {
    if(packet==0x029){
        if(size>=0x1C)message(context,sink,u32(data+8),(data[24]|(unsigned(data[25])<<8))&0x7FFF,u32(data+12));
        return;
    }
    if(packet!=0x028||size<19)return;
    Bits bits{data,size};
    const auto actor=bits.Take(32),targets=bits.Take(6);
    bits.Take(4);const auto category=bits.Take(4),argument=bits.Take(32);bits.Take(32);
    if(category!=1&&category!=2&&category!=3&&category!=4&&category!=6&&category!=11
        &&category!=13&&category!=14&&category!=15)return;
    for(unsigned t=0;t<targets&&bits.valid;++t){
        const auto target=bits.Take(32),actions=bits.Take(4);
        if(!actions||actions>8)return; // Same maximum as the client; reject truncated/invalid records.
        for(unsigned a=0;a<actions&&bits.valid;++a){
            bits.Take(27);const auto value=bits.Take(17),msg=bits.Take(10);bits.Take(31);
            unsigned procValue=0,procMessage=0,reactValue=0,reactMessage=0;
            if(bits.Take(1)){bits.Take(10);procValue=bits.Take(17);procMessage=bits.Take(10);}
            if(bits.Take(1)){bits.Take(10);reactValue=bits.Take(14);reactMessage=bits.Take(10);}
            if(!bits.valid)return;
            // Counter/spikes damage and reaction status apply to the attacker.
            if(msg==33||msg==44||msg==132||msg==536){if(value)sink(context,{actor,DebuffChange::Damage,0});}
            else message(context,sink,target,msg,value,argument,false,category);
            // Dia/Bio report damage, not a separate status result. Their known
            // overwrite order is Dia I < Bio I < Dia II < Bio II, etc.
            if(category==4&&(msg==2||msg==252||msg==264||msg==265)){
                if(argument>=23&&argument<=27)sink(context,{target,DebuffChange::Add,134,(argument-23)*2+1,argument<=25?60*(argument-22):300});
                else if(argument>=33&&argument<=37)sink(context,{target,DebuffChange::Add,134,(argument-33)*2+1,argument<=35?60*(argument-32):300});
                else if(argument>=230&&argument<=234)sink(context,{target,DebuffChange::Add,135,(argument-230)*2+2,argument<=232?60*(argument-229):300});
                else if((argument>=278&&argument<=285)||(argument>=885&&argument<=892))
                    sink(context,{target,DebuffChange::Add,186,0,90});
            }
            message(context,sink,target,procMessage,procValue,category==4?argument:0,true);
            if(reactMessage==44||reactMessage==132){if(reactValue)sink(context,{actor,DebuffChange::Damage,0});}
            else message(context,sink,actor,reactMessage,reactValue,0,true);
        }
    }
}
void Debuffs::Clear(){
    const std::lock_guard lock(mutex_);
    for(auto& row:rows_)row.id.store(0,std::memory_order_release);
}
void Debuffs::Forget(unsigned index,std::uint32_t id){
    if(index>=0x900)return;
    const std::lock_guard lock(mutex_);
    if(rows_[index].id.load()==id)rows_[index].id.store(0,std::memory_order_release);
}
void Debuffs::Apply(unsigned index,std::uint32_t id,DebuffChange change,unsigned effect,std::uint32_t now,unsigned rank,unsigned duration,bool preserveLonger){
    if(index>=0x900||!id)return;
    const std::lock_guard lock(mutex_);
    auto& row=rows_[index];
    if(row.id.load()!=id){
        if(change!=DebuffChange::Add)return;
        row.id.store(0,std::memory_order_release);
        for(auto& item:row.effects)item.store(0,std::memory_order_relaxed);
        row.id.store(id,std::memory_order_release);
    }
    if(change==DebuffChange::Defeat){row.id.store(0,std::memory_order_release);return;}
    if(change==DebuffChange::Add&&DebuffCell(effect)==~0u)return;
    // Keep only the winning observed Dia/Bio. Earlier, unseen effects remain unknown.
    if(change==DebuffChange::Add&&rank)for(auto& item:row.effects){
        const auto value=item.load(std::memory_order_relaxed);
        const auto old=static_cast<unsigned>(value)&0xFFFF;
        if(alive(value,now)&&(old==134||old==135)){
            if(((value>>16)&0xFFFF)>rank)return;
            if(old!=effect)item.store(0,std::memory_order_relaxed);
        }
    }
    unsigned available=MaxDebuffs;
    for(unsigned i=0;i<MaxDebuffs;++i){
        const auto value=row.effects[i].load(std::memory_order_relaxed);
        const auto old=static_cast<unsigned>(value)&0xFFFF;
        if(!alive(value,now)){if(available==MaxDebuffs)available=i;continue;}
        const bool same=family(old)==family(effect);
        if(change==DebuffChange::Damage?(old==2||old==19||old==193):same){
            if(change==DebuffChange::Add&&preserveLonger&&static_cast<std::uint32_t>(value>>32)-now>duration)return;
            row.effects[i].store(change==DebuffChange::Add?(std::uint64_t(now+duration)<<32)|(rank<<16)|effect:0,std::memory_order_relaxed);
            if(change!=DebuffChange::Damage)return;
        }
    }
    if(change==DebuffChange::Add&&available<MaxDebuffs)
        row.effects[available].store((std::uint64_t(now+duration)<<32)|(rank<<16)|effect,std::memory_order_relaxed);
}
DebuffRow Debuffs::Read(unsigned index,std::uint32_t id,std::uint32_t now) const noexcept {
    DebuffRow result;
    if(index>=0x900||!id)return result;
    const auto& row=rows_[index];
    if(row.id.load(std::memory_order_acquire)!=id)return result;
    for(const auto& item:row.effects){
        const auto value=item.load(std::memory_order_relaxed);
        if(alive(value,now))result.effects[result.count++]=static_cast<std::uint16_t>(value);
    }
    if(row.id.load(std::memory_order_acquire)!=id)return {};
    return result;
}
}
