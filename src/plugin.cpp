// Independent prototype. Layout/drawing is ours; assets and scene placement are
// supplied by the original game. MobDB trait data/artwork is embedded separately;
// no addon/plugin implementation is incorporated.
#include <Ashita.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstddef>
#include <new>
#include <cmath>
#include <filesystem>
#include <ffxi/entity.h>
#include <ffxi/target.h>
#include "cursor.h"
#include <ffxi/enums.h>
#include "layout.h"
#include "health.h"
#include "levels.h"
#include "traits.h"
#include "trait_texture.h"
#include "options.h"
#include "client_profile.h"
#include "native_contract.h"

extern "C" {
std::uintptr_t NameplateResume=0,NameplateExit=0,DamageResume=0;
std::uintptr_t MenuDrawAddress=0,TargetWindowAddress=0;
void NameplateGate();
void DamageGate();
void __fastcall CursorMenuGate(std::uintptr_t,void*) noexcept;
std::uintptr_t NameplateSubmitResume=0;
std::uintptr_t CursorTailAddress=0;
// Exact native cursor tail: ESI=window, [esp+8]=selected actor. Its epilogue
// restores EDI/ESI and discards the two locals reserved here.
__declspec(naked) void __fastcall DrawNativeCursorTail(std::uintptr_t, std::uintptr_t) {
    __asm {
        sub esp, 8
        push esi
        push edi
        mov esi, ecx
        mov [esp+8], edx
        jmp dword ptr [CursorTailAddress]
    }
}
#include "submission_gate.h"
}

namespace {
using namespace nameplate_lab;
constexpr auto HookRva=native_contract::NameHook;
constexpr std::uint32_t SubmitRva=0xCD00, SubmitPrefixSize=7;
std::atomic<bool> submissionDetoured{false};
constexpr unsigned char Original[6]={0x33,0xD2,0x33,0xED,0x33,0xF6};
std::uintptr_t clientBase=0;
constexpr auto DamageRva=native_contract::DamageHook;
constexpr unsigned char DamageOriginal[6]={0x81,0xEC,0x24,0x03,0x00,0x00};
unsigned char damagePatch[6]{};
bool damageHooked=false,damageHookAttempted=false;
constexpr auto CursorRva=native_contract::CursorCall;
constexpr unsigned char CursorOriginal[5]={0xE8,0xA2,0x80,0xFB,0xFF};
unsigned char cursorPatch[5]{};
bool cursorHooked=false,cursorAttempted=false,cursorReady=false;
std::atomic<bool> keepCursor{false};
HMODULE retainedModule=nullptr; // Only a failed teardown retains code; normal unload frees the DLL.
void ConfigureNative(std::uintptr_t base);
CursorClearance cursorClearance[2]; // Cleared every scene; future above-name content contributes here.
unsigned cursorDraws=0;
char cursorProblem[160]="off";
std::atomic<bool> damageFault{false};
std::atomic<bool> damageEnabled{false},damageCorrectAspect{false};
std::atomic<float> damageScale{1},damageWidth{1};
std::atomic<unsigned> damageAdjusted{0},damageRejected{0};
std::atomic<unsigned> mode{0}, requested{0}, instances{0}; // 0 original, 1 self, 2 all, 3 enemy HP.
std::atomic<unsigned> replaced{0}, rejected{0}, drawingErrors{0};
std::atomic<unsigned> healthNames{0}, filteredNames{0};
std::atomic<bool> nameFault{false};
bool intentionallyFiltered=false;
std::atomic<float> nameScale{1}, nameWidth{1};
std::atomic<bool> correctAspect{true};
std::atomic<unsigned> nameFilter{2};
std::atomic<bool> showNameStatusIcons{true};
std::atomic<bool> showLevels{true},autoCheck{true};
std::atomic<bool> levelsTargetOnly{false},traitsTargetOnly{false};
struct DetailTarget { unsigned index=0;std::uint32_t id=0; } detailTarget;
Levels levels;
std::atomic<float> levelScale{1};
std::atomic<bool> showTraits{true};
std::atomic<float> traitScale{1};
std::atomic<unsigned> traitZone{0}; // Initial SDK zone, then zone-transition packets.
TraitTexture traitTexture;
HRESULT traitTextureResult=S_OK;
void PublishVisuals(const Options& options) noexcept {
    keepCursor=options.keepCursor;
    showLevels=options.showLevels;autoCheck=options.autoCheck;levelScale=options.levelScale;
    showTraits=options.showTraits;traitScale=options.traitScale;
    levelsTargetOnly=options.levelsTargetOnly;traitsTargetOnly=options.traitsTargetOnly;
    damageEnabled.store(options.damageEnabled&&!damageFault.load());
    damageCorrectAspect.store(options.damageCorrectAspect);
    damageScale.store(options.damageScale);damageWidth.store(options.damageWidth);
    nameScale.store(options.scale,std::memory_order_relaxed);
    nameWidth.store(options.width,std::memory_order_relaxed);
    correctAspect.store(options.correctAspect,std::memory_order_relaxed);
    nameFilter.store(options.filter,std::memory_order_relaxed);
    showNameStatusIcons.store(options.showStatusIcons,std::memory_order_relaxed);
}
Options Visuals() noexcept {
    Options value;
    value.scale=nameScale.load(std::memory_order_relaxed);
    value.width=nameWidth.load(std::memory_order_relaxed);
    value.correctAspect=correctAspect.load(std::memory_order_relaxed);
    value.filter=nameFilter.load(std::memory_order_relaxed);
    value.showStatusIcons=showNameStatusIcons.load(std::memory_order_relaxed);
    return value;
}
unsigned char patch[6]{};
bool hooked=false;
char lastProblem[160]="not enabled";
static_assert(offsetof(Ashita::FFXI::entity_t, HPPercent) == 0xEC);
static_assert(offsetof(Ashita::FFXI::entity_t, SpawnFlags) == 0x1D0);
static_assert(offsetof(Ashita::FFXI::entity_t, ServerId) == 0x78);
constexpr auto StatusFlagsOffset=offsetof(Ashita::FFXI::entity_t,Render)+offsetof(Ashita::FFXI::render_t,Flags1);
static_assert(StatusFlagsOffset==0x124 && offsetof(Ashita::FFXI::entity_t,LinkshellColor)==0x1D4);
static_assert(offsetof(Ashita::FFXI::render_t,Flags2)==offsetof(Ashita::FFXI::render_t,Flags1)+4);
static_assert(offsetof(Ashita::FFXI::targetwindow_t,m_pAnkShape)==0x78
    &&offsetof(Ashita::FFXI::targetwindow_t,m_Sub)==0xB8
    &&offsetof(Ashita::FFXI::targetwindow_t,m_AnkY)==0xBE);
constexpr auto EnemyFlag = static_cast<std::uint32_t>(Ashita::FFXI::Enums::EntitySpawnFlags::Monster);
// Player, party, alliance, ally, local player, fellow, and Trust.
constexpr std::uint32_t FriendlyFlags = 0x1 | 0x4 | 0x8 | 0x100 | 0x200 | 0x800 | 0x1000;

bool ReadBytes(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!address || size>0x10000) return false;
    __try { std::memcpy(output,reinterpret_cast<const void*>(address),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address,T& output) noexcept { return ReadBytes(address,&output,sizeof(output)); }
template<class T> T PacketValue(const std::uint8_t* data,unsigned offset) noexcept {
    T value;std::memcpy(&value,data+offset,sizeof(value));return value;
}
std::uint32_t LocalIdentity() noexcept {
    std::uint16_t index=0;std::uint32_t entity=0,id=0;
    if(Read(clientBase+0x47D604,index)&&index&&index<0x900
        &&Read(clientBase+0x480AF0+index*4,entity)&&entity)Read(entity+0x78,id);
    return id;
}
bool CheckTarget(Ashita::FFXI::targetentry_t& target) noexcept {
    std::uint32_t controller=0,id=0,entity=0,flags=0;std::uint8_t hp=0;
    return Read(clientBase+0x57876C,controller)&&controller
        &&Read(controller,target)&&target.IsActive&&target.ServerId&&target.Index<0x900
        &&Read(clientBase+0x480AF0+target.Index*4,entity)&&entity&&entity==target.EntityPointer
        &&Read(entity+0x78,id)&&id==target.ServerId
        &&Read(entity+offsetof(Ashita::FFXI::entity_t,SpawnFlags),flags)
        &&(flags&EnemyFlag)&&!(flags&FriendlyFlags)
        &&Read(entity+offsetof(Ashita::FFXI::entity_t,HPPercent),hp)&&hp>0&&hp<=100;
}
void RefreshDetailTarget() noexcept {
    // One current-scene identity for both filters. Collection already validates
    // each visible entity, so do not repeat entity/HP reads or cache pointers.
    detailTarget={};
    if(requested.load()<2||!((showLevels.load()&&levelsTargetOnly.load())
        ||(showTraits.load()&&traitsTargetOnly.load()&&traitTexture.value)))return;
    std::uint32_t controller=0;
    Ashita::FFXI::targetentry_t target;
    if(Read(clientBase+0x57876C,controller)&&controller&&Read(controller,target)&&target.IsActive)
        detailTarget={target.Index,target.ServerId};
}
bool Problem(const char* value) { strcpy_s(lastProblem,value); return false; }

// Called at the render boundary (BeginScene setup / Ashita Present teardown).
// No thread suspension: these sites execute synchronously in native drawing.
bool ExchangeSite(const unsigned char* expected,const unsigned char* next,std::uint32_t siteRva=HookRva,unsigned siteSize=6) noexcept {
    auto* destination=reinterpret_cast<unsigned char*>(clientBase+siteRva);
    unsigned char current[6];DWORD protection=0,ignored=0;
    if(!ReadBytes(reinterpret_cast<std::uintptr_t>(destination),current,siteSize))return Problem("unreadable patch site");
    if(std::memcmp(current,expected,siteSize))return Problem("patch ownership changed");
    if(!VirtualProtect(destination,siteSize,PAGE_EXECUTE_READWRITE,&protection))return Problem("patch protection change failed");
    std::memcpy(destination,next,siteSize);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),destination,siteSize)!=FALSE;
    if(!flushed){
        std::memcpy(destination,expected,siteSize);
        FlushInstructionCache(GetCurrentProcess(),destination,siteSize);
    }
    // The bytes are already changed; a protection-restore failure must not cause
    // the caller to forget an installed hook. Protection itself is not executable code.
    const bool protectedAgain=VirtualProtect(destination,siteSize,protection,&ignored)!=FALSE;
    if(!flushed)return Problem("instruction flush failed; original patch retained");
    if(!protectedAgain)strcpy_s(lastProblem,"patch applied; page protection restore failed");
    return true;
}

bool Compatible() noexcept {
    if(!clientBase)return Problem("FFXiMain.dll is not loaded");
    for(const auto& range:client_profile::ranges){
        // All compared bytes are populated below; no full-buffer clearing needed.
        unsigned char expected[2048],actual[2048];
        if(range.size>sizeof(expected))return Problem("internal profile size");
        std::memcpy(expected,range.bytes,range.size);
        for(unsigned i=0;i<range.relocationCount;++i){
            std::uint32_t value=0;const auto off=range.relocations[i];
            std::memcpy(&value,expected+off,4);
            value+=static_cast<std::uint32_t>(clientBase)-0x10000000u;
            std::memcpy(expected+off,&value,4);
        }
        if(hooked && range.rva<=HookRva && HookRva+6<=range.rva+range.size)
            std::memcpy(expected+HookRva-range.rva,patch,6);
        if(damageHooked && range.rva==DamageRva)std::memcpy(expected,damagePatch,6);
        if(!ReadBytes(clientBase+range.rva,actual,range.size))return Problem("unreadable client name/render routine");
        unsigned skip=0;
        if(range.rva==SubmitRva){
            // We execute our own verified prologue, so a seven-byte entry detour
            // is outside our path. Every byte of the original body stays exact.
            const bool native=std::memcmp(expected,actual,SubmitPrefixSize)==0;
            const bool detour=actual[0]==0xE9&&actual[5]==0x90&&actual[6]==0x90;
            if(!native&&!detour)return Problem("unsupported submission entry layout");
            submissionDetoured.store(!native,std::memory_order_relaxed);
            skip=SubmitPrefixSize;
        }
        if(std::memcmp(expected+skip,actual+skip,range.size-skip)!=0)
            return Problem("unsupported or modified client name/render body");
    }
    const unsigned char bases[6]={169,169,169,169,169,169},counts[6]={0,1,2,0,1,2};
    unsigned char bytes[6]{};
    if(!ReadBytes(clientBase+0x32D434,bytes,6)||std::memcmp(bytes,bases,6)!=0
        ||!ReadBytes(clientBase+0x32D43C,bytes,6)||std::memcmp(bytes,counts,6)!=0)
        return Problem("unsupported icon expansion tables");
    return true;
}

// Setup validates the complete profile. Steady rendering only checks ownership;
// it does not continuously police unrelated bytes in already-validated routines.
bool OwnsHooks() noexcept {
    unsigned char site[6];
    if(hooked&&(!ReadBytes(clientBase+HookRva,site,6)||std::memcmp(site,patch,6)))
        return Problem("name hook ownership changed");
    if(damageHooked&&(!ReadBytes(clientBase+DamageRva,site,6)||std::memcmp(site,damagePatch,6)))
        return Problem("damage hook ownership changed");
    return true;
}

bool InstallCursor() noexcept {
    const auto fail=[](const char* reason){strcpy_s(cursorProblem,reason);return false;};
    for(const auto& range:client_profile::cursorRanges){
        unsigned char expected[2048],actual[2048];
        if(range.size>sizeof(expected))return fail("internal cursor profile size");
        std::memcpy(expected,range.bytes,range.size);
        for(unsigned i=0;i<range.relocationCount;++i){
            const auto off=range.relocations[i];std::uint32_t value;
            std::memcpy(&value,expected+off,4);value+=static_cast<std::uint32_t>(clientBase)-0x10000000u;
            std::memcpy(expected+off,&value,4);
        }
        if(cursorHooked&&range.rva<=CursorRva&&CursorRva+5<=range.rva+range.size)
            std::memcpy(expected+CursorRva-range.rva,cursorPatch,5);
        if(!ReadBytes(clientBase+range.rva,actual,range.size))return fail("unreadable native cursor path");
        // Observed shared-menu entry detour. We call that same entry, preserving
        // its owner, rather than bypassing it. Validate the unchanged body once.
        const unsigned skip=range.rva==native_contract::MenuDraw&&actual[0]==0xE9?5u:0u;
        if(std::memcmp(expected+skip,actual+skip,range.size-skip))
            return fail("unsupported or modified native cursor path");
    }
    if(!cursorHooked&&!ExchangeSite(CursorOriginal,cursorPatch,CursorRva,5))return fail(lastProblem);
    cursorHooked=true;cursorReady=true;strcpy_s(cursorProblem,"ready");return true;
}

// The native UI dispatcher has already applied menu/cutscene filtering. Hidden
// panel flags only suppress its frame and draw callback; we leave both untouched.
unsigned __stdcall RenderCursorMenu(std::uintptr_t menu) noexcept {
    if(!keepCursor||!cursorReady)return 0;
    std::uint32_t window=0,controller=0;
    unsigned char callbackVisible=0;
    const bool hasClearance=cursorClearance[0].occupied||cursorClearance[1].occupied;
    if(!Read(menu+0x6A,callbackVisible))return 0;
    if(callbackVisible&&!hasClearance)return 0; // Ordinary visible cursor needs no work.
    Ashita::FFXI::targetwindow_t state;
    Ashita::FFXI::targetentry_t targets[2];
    // The native gate already matched this menu's callback to the target window.
    if(!Read(menu+0xC,window)||!window||!ReadBytes(window,&state,sizeof(state))
        ||!Read(clientBase+0x57876C,controller)||!controller
        ||!ReadBytes(controller,targets,sizeof(targets))||state.DeathFlag||state.m_AnkNum>=16)return 0;
    // Target identities change on selection/despawn. These are live-data checks,
    // confined to at most two arrows, not a scan of actors or names.
    if(!targets[0].IsActive||!targets[0].ActorPointer)return 0;
    for(unsigned i=0;i<(state.m_Sub?2u:1u);++i){
        if(!targets[i].IsActive)continue;
        std::uint32_t id=0;
        if(!targets[i].EntityPointer||!Read(targets[i].EntityPointer+0x78,id)||id!=targets[i].ServerId)return 0;
    }
    // Match the generic menu renderer's modal visibility gate before forcing a
    // hidden callback. Without this, forcing would bypass that native early exit.
    std::uint32_t modal=0,modalMenu=0;unsigned char modalActive=0,menuFlags=0;
    if(!Read(clientBase+0x5781CC,modal)||!modal||!Read(modal+0x48,modalActive))return 0;
    if(modalActive&&(!Read(modal+8,modalMenu)||!Read(menu+0x34,menuFlags)
        ||(modalMenu!=menu&&!(menuFlags&0x40))))return 0;
    const auto shape=state.m_pAnkShape[state.m_AnkNum];
    if(!shape)return 0; // Native resources can be absent during window teardown.
    std::int16_t bottom=0;
    if(hasClearance&&!Read(shape+0x26,bottom))return 0;
    const unsigned main=state.m_Sub?1u:0u;
    const int mainLift=cursorClearance[main].Lift(targets[main].ServerId,state.m_AnkY+bottom-0.5f);
    const int subLift=state.m_Sub?cursorClearance[0].Lift(targets[0].ServerId,state.m_SubAnkY+bottom-0.5f):0;
    // Temporary coordinates affect only this call. Native animation, position
    // updates, resources and main/subtarget colors remain the client's own.
    auto* live=reinterpret_cast<Ashita::FFXI::targetwindow_t*>(window);
    if(mainLift)live->m_AnkY=static_cast<std::int16_t>((std::max)(-32768,static_cast<int>(state.m_AnkY)-mainLift));
    if(subLift)live->m_SubAnkY=static_cast<std::int16_t>((std::max)(-32768,static_cast<int>(state.m_SubAnkY)-subLift));
    reinterpret_cast<void(__thiscall*)(std::uintptr_t)>(clientBase+native_contract::MenuDraw)(menu);
    if(!callbackVisible){DrawNativeCursorTail(window,targets[0].ActorPointer);++cursorDraws;}
    if(mainLift)live->m_AnkY=state.m_AnkY;
    if(subLift)live->m_SubAnkY=state.m_SubAnkY;
    return 1;
}

// Validate the exact native caller and its
// stack-owned scale pair. No retained pointers, textures, or draw interception.
unsigned __stdcall AdjustDamage(std::uintptr_t frame) noexcept {
    std::uint32_t caller=0,pair=0;
    float scales[2]{};
    if(!Read(frame,caller)||caller!=clientBase+0x41C0F||!Read(frame+16,pair)
        ||pair!=frame+0x38||!ReadBytes(pair,scales,sizeof(scales))
        ||!std::isfinite(scales[0])||!std::isfinite(scales[1])
        ||scales[0]<=0||scales[1]<=0||scales[0]>128||scales[1]>128){
        damageRejected.fetch_add(1);return 0;
    }
    if(!damageEnabled.load())return 0;

    Options options;options.scale=damageScale.load();options.width=damageWidth.load();
    options.correctAspect=damageCorrectAspect.load();
    if(!SizeScale(scales[0],scales[1],options)){damageRejected.fetch_add(1);return 0;}
    // The pointer is proven to be this native call's stack, not arbitrary memory.
    __try {
        auto* output=reinterpret_cast<float*>(pair);
        output[0]=scales[0];output[1]=scales[1];
    }__except(EXCEPTION_EXECUTE_HANDLER){damageRejected.fetch_add(1);return 0;}
    damageAdjusted.fetch_add(1);return 1;
}

struct Resources {
    void* graphics; IDirect3DDevice8* device; IDirect3DBaseTexture8* textures[3];
    unsigned healthPercent = 100;
    LevelLabel level;
    TraitLabel traits;
};
bool Collect(std::uintptr_t frame,Input& input,Resources& resources,StatusIcons* icons=nullptr,bool showIcons=true) noexcept {
    if(icons)*icons={};
    std::uint32_t caller=0,actor=0,entity=0,identity=0,current=0;
    if(!Read(frame+0x6D4,caller)||caller!=clientBase+0xD08A8 || !Read(frame+4,actor)
        ||!Read(actor+0x70,entity)||!Read(entity+0x78,identity)||!identity)return false;
    std::uint16_t entityIndex=0;
    if(!Read(entity+0x74,entityIndex)||entityIndex>=0x900
        ||!Read(clientBase+0x480AF0+entityIndex*4,current)||current!=entity)return false;
    if(mode.load(std::memory_order_relaxed)==1){
        std::uint16_t playerIndex=0;
        if(!Read(clientBase+0x47D604,playerIndex)||!playerIndex)return false;
        if(entityIndex!=playerIndex){intentionallyFiltered=true;return false;}
    }
    std::uint32_t spawnFlags=0;
    if((icons||mode.load(std::memory_order_relaxed)==3)
        &&!Read(entity+offsetof(Ashita::FFXI::entity_t,SpawnFlags),spawnFlags))return false;
    if(mode.load(std::memory_order_relaxed)==3){
        std::uint8_t hp=100;
        if((spawnFlags&EnemyFlag)&&!(spawnFlags&FriendlyFlags)
            &&Read(entity+offsetof(Ashita::FFXI::entity_t,HPPercent),hp)&&hp<=100)
            resources.healthPercent=hp;
    }
    std::uint32_t text=0,length=0;
    if(!Read(frame+0x18,length)||!Read(frame+0x6DC,text)||length>36)return false;
    input.length=length;
    if(!ReadBytes(text,input.text,length)||!Read(frame+0x30,input.x)||!Read(frame+0x34,input.y)
        ||!Read(frame+0x48,input.z)||!Read(frame+0x4C,input.scaleX)||!Read(frame+0x50,input.scaleY)
        ||!Read(frame+0x6E4,input.nameColor)||!Read(frame+0x6E8,input.shellColor))return false;
    // Unusual multiline labels retain native icon placement and colors.
    bool singleLine=true;
    for(unsigned i=0;i<length;++i)if(input.text[i]==10){resources.healthPercent=100;singleLine=false;}
    const bool selected=detailTarget.id==identity&&detailTarget.index==entityIndex;
    if(showLevels.load(std::memory_order_relaxed)&&(!levelsTargetOnly.load(std::memory_order_relaxed)||selected)
        &&singleLine&&(spawnFlags&EnemyFlag)&&!(spawnFlags&FriendlyFlags))
    {
        resources.level=levels.Label(entityIndex,identity);
        resources.level.scale=levelScale.load(std::memory_order_relaxed);
    }
    if(showTraits.load(std::memory_order_relaxed)&&(!traitsTargetOnly.load(std::memory_order_relaxed)||selected)
        &&traitTexture.value&&singleLine
        &&(spawnFlags&EnemyFlag)&&!(spawnFlags&FriendlyFlags)){
        // Use the already validated visible entity; never scan actors or retain
        // its native pointer. Only the first 24 bytes are the entity name.
        char name[25]{};
        if(ReadBytes(entity+offsetof(Ashita::FFXI::entity_t,Name),name,24)){
            resources.traits.bits=LookupTraits(traitZone.load(std::memory_order_relaxed),entityIndex,name);
            resources.traits.scale=traitScale.load(std::memory_order_relaxed);
            resources.textures[2]=traitTexture.value;
        }
    }
    if(icons&&(spawnFlags&1)&&singleLine){
        icons->replace=true;
        if(showIcons){
            std::uint32_t flags[2]; // Adjacent Render.Flags1 and Flags2, one snapshot.
            if(!ReadBytes(entity+StatusFlagsOffset,flags,sizeof(flags)))return false;
            // Live seeking/bazaar flags match native tests 0x97683/0x976DF and
            // linkshell candidate 0x97875. Read independently, without priority.
            icons->active=static_cast<std::uint8_t>(((flags[0]&0x00100000)?1:0)
                |((flags[1]&0x00000200)?2:0)|((flags[0]&0x08000000)?4:0));
            if((icons->active&4)&&!Read(entity+offsetof(Ashita::FFXI::entity_t,LinkshellColor),icons->linkshellColor))return false;
        }
    }
    if(!ReadBytes(clientBase+0x32D434,input.expansionBase,6)||!ReadBytes(clientBase+0x32D43C,input.expansionCount,6))return false;
    std::uint8_t codes[MaxGlyphs];unsigned count=0,nameCount=0;
    if(!ExpandName(input,codes,count,nameCount,icons))return false;
    std::uint32_t fontData[12]{};
    if(!ReadBytes(clientBase+0x4E1BF8,fontData,sizeof(fontData))||fontData[9]!=32||fontData[10]!=142)return false;
    std::uint32_t table=0;
    if(!Read(fontData[11],table)||!table)return false;
    const auto loadGlyph=[&](std::uint8_t code){
        if(code==10)return true;
        if(code<32)return false;
        auto& g=input.glyphs[code];if(g.valid)return true;
        std::uint32_t shape=0,node=0,part=0;
        if(!Read(table+(code-32)*4,shape)||!shape||!Read(shape+4,node))return false;
        bool found=false;
        for(unsigned step=0;node&&step<64;++step){
            unsigned char disabled=0;if(!Read(node+0x14,disabled))return false;
            if(!disabled){if(!Read(node+0x10,part)||!part)return false;found=true;break;}
            if(!Read(node,node))return false;
        }
        if(!found)return false;
        // Snapshot adjacent fields once; keep no native glyph pointers between draws.
        std::int16_t bounds[7],offsets[3];
        unsigned char vertices[92]; // Four UV pairs, separated by native vertex stride.
        if(!ReadBytes(part+0xC0,bounds,sizeof(bounds))
            ||!ReadBytes(shape+0x20,offsets,sizeof(offsets))
            ||!ReadBytes(part+0x64,vertices,sizeof(vertices)))return false;
        const int width=bounds[1]-bounds[0]-1,height=bounds[6]-bounds[4]-1;
        if(width<0||width>256||height<0||height>256)return false;
        g.offsetX=offsets[0];g.offsetY=offsets[2];
        g.width=static_cast<std::int16_t>(width);g.height=static_cast<std::int16_t>(height);
        for(unsigned v=0;v<4;++v)std::memcpy(g.uv+v*2,vertices+v*28,8);
        g.textureGroup=code>=142?1:0;g.valid=1;
        if(!resources.textures[g.textureGroup]){
            const auto descriptor=fontData[7+g.textureGroup];std::uint32_t texture=0;
            if(!descriptor||!Read(descriptor+0x40,texture))return false;
            if(!texture&&!Read(descriptor+0x44,texture))return false;
            if(!texture)return false;
            resources.textures[g.textureGroup]=reinterpret_cast<IDirect3DBaseTexture8*>(texture);
        }
        return true;
    };
    const unsigned readCount=count+(count>nameCount?1u:0u);
    for(unsigned i=0;i<readCount;++i)
        if(!loadGlyph(i<count?codes[i]:std::uint8_t(32)))return false;
    if(resources.level.length){
        bool ready=loadGlyph(32);
        for(unsigned i=0;ready&&i<resources.level.length;++i)
            ready=loadGlyph(static_cast<std::uint8_t>(resources.level.text[i]));
        if(!ready)resources.level={}; // Missing optional glyphs do not suppress the name.
    }
    if(resources.traits.bits&&!loadGlyph(32))resources.traits={};
    std::uint32_t graphics=0,device=0;
    if(!Read(clientBase+0x45666C,graphics)||!graphics||!Read(graphics+0xC,device)||!device)return false;
    resources.graphics=reinterpret_cast<void*>(graphics);resources.device=reinterpret_cast<IDirect3DDevice8*>(device);
    return true;
}

struct SizingReference {
    unsigned screenWidth=0,screenHeight=0;
    float width=0;
};
// Both presets depend only on display dimensions, sampled when requested.
bool ReadSizingReference(SizingReference& reference) noexcept {
    std::uint32_t config=0;
    std::uint16_t screen[2];
    if(!Read(clientBase+0x4568FC,config)||!config||!ReadBytes(config+0x10,screen,sizeof(screen)))return false;
    reference.screenWidth=screen[0];reference.screenHeight=screen[1];
    return OriginalWidth(reference.screenWidth,reference.screenHeight,reference.width);
}

using Submit=HRESULT(__thiscall*)(void*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
// Scope only our draw: never leave another UI element using our sampler choice.
struct DrawProgress {
    unsigned submitted=0;
    bool nativeSafe=true;
    const char* operation="none";
    HRESULT error=S_OK;
    bool Check(HRESULT result,const char* name) noexcept {
        if(SUCCEEDED(result))return true;
        if(SUCCEEDED(error)){error=result;operation=name;}
        return false;
    }
};
DrawProgress lastDrawFailure;
// Only a recognized lost-device error resumes automatically. No device polling
// during healthy drawing, and no retained device/texture pointers.
bool DrawingDeviceRecovered() noexcept {
    if(!nameFault.load()||(lastDrawFailure.error!=D3DERR_DEVICELOST
        &&lastDrawFailure.error!=D3DERR_DEVICENOTRESET))return false;
    std::uint32_t graphics=0,device=0;
    return Read(clientBase+0x45666C,graphics)&&graphics&&Read(graphics+0xC,device)&&device
        &&reinterpret_cast<IDirect3DDevice8*>(device)->TestCooperativeLevel()==D3D_OK;
}
struct FilterScope {
    IDirect3DDevice8* device;
    DrawProgress& progress;
    DWORD min=0,mag=0;
    bool changed=false;
    bool Apply(unsigned filter) noexcept {
        if(!filter)return true;
        if(!progress.Check(device->GetTextureStageState(0,D3DTSS_MINFILTER,&min),"Get MINFILTER")
            ||!progress.Check(device->GetTextureStageState(0,D3DTSS_MAGFILTER,&mag),"Get MAGFILTER"))return false;
        changed=true;progress.nativeSafe=false;
        const auto value=filter==1?D3DTEXF_POINT:D3DTEXF_LINEAR;
        return progress.Check(device->SetTextureStageState(0,D3DTSS_MINFILTER,value),"Set MINFILTER")
            &&progress.Check(device->SetTextureStageState(0,D3DTSS_MAGFILTER,value),"Set MAGFILTER");
    }
    bool Restore() noexcept {
        if(!changed)return true;
        const bool a=progress.Check(device->SetTextureStageState(0,D3DTSS_MINFILTER,min),"Restore MINFILTER");
        const bool b=progress.Check(device->SetTextureStageState(0,D3DTSS_MAGFILTER,mag),"Restore MAGFILTER");
        changed=!(a&&b);
        return !changed;
    }
};
bool Draw(const Output& output,const Resources& resources,unsigned* submitted=nullptr,unsigned filter=0,DrawProgress* report=nullptr) noexcept {
    DrawProgress progress;
    auto* device=resources.device;
    FilterScope sampler{device,progress};
    const auto finish=[&](bool success){
        const bool restored=sampler.Restore();
        if(submitted)*submitted=progress.submitted;
        if(report)*report=progress;
        return success&&restored;
    };
    if(!sampler.Apply(filter))return finish(false);
    const auto submit=reinterpret_cast<Submit>(&NameplateSubmit);
    const auto health=resources.healthPercent<100?MeasureHealth(output,resources.healthPercent):HealthFill{};
    for(unsigned i=0;i<output.count;++i){
        Quad pieces[2];
        const auto* quads=&output.quads[i];
        unsigned pieceCount=1;
        if(health.enabled){pieceCount=HealthQuads(*quads,health,pieces);quads=pieces;}
        for(unsigned piece=0;piece<pieceCount;++piece){
            const auto& q=quads[piece];
            // After state mutation or a submission, conservatively consume this
            // name. Never submit native geometry over a partial replacement.
            progress.nativeSafe=false;
            if(!progress.Check(device->SetTexture(0,resources.textures[q.textureGroup]),"SetTexture")
                ||!progress.Check(device->SetRenderState(D3DRS_ALPHAREF,q.alphaReference),"Set ALPHAREF")
                ||!progress.Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"Set ALPHABLENDENABLE")
                ||!progress.Check(device->SetVertexShader(0x144),"SetVertexShader")
                ||!progress.Check(submit(resources.graphics,D3DPT_TRIANGLESTRIP,2,q.vertices,sizeof(Vertex)),"Submit")){
                progress.Check(device->SetRenderState(D3DRS_ALPHAREF,0),"Restore ALPHAREF");
                return finish(false);
            }
            ++progress.submitted;
            if(!progress.Check(device->SetRenderState(D3DRS_ALPHAREF,0),"Restore ALPHAREF"))return finish(false);
        }
    }
    return finish(true);
}
}

extern "C" {
// No dynamic allocation or plugin-object access occurs on the native draw stack.
unsigned __stdcall RenderName(std::uintptr_t frame) noexcept {
    unsigned result=0;
    if(mode.load(std::memory_order_acquire)){
        nameplate_lab::Input input{};nameplate_lab::Output output;Resources resources{};StatusIcons icons;
        const auto visuals=Visuals();
        intentionallyFiltered=false;
        if(Collect(frame,input,resources,&icons,visuals.showStatusIcons)&&SizeName(input,visuals)&&nameplate_lab::Build(input,output,&icons,&resources.level,&resources.traits)){
            // Once drawing starts, do not redraw the original on top of a partial
            // replacement. A device error turns the feature off for future names.
            result=1;
            DrawProgress progress;
            const bool drawn=Draw(output,resources,nullptr,visuals.filter,&progress);
            if(drawn){
                replaced.fetch_add(1,std::memory_order_relaxed);
                if(resources.healthPercent<100)healthNames.fetch_add(1,std::memory_order_relaxed);
            }else{nameFault.store(true);lastDrawFailure=progress;result=progress.nativeSafe?0:1;drawingErrors.fetch_add(1,std::memory_order_relaxed);mode.store(0);requested.store(0);}
        }else if(intentionallyFiltered)filteredNames.fetch_add(1);
        else rejected.fetch_add(1,std::memory_order_relaxed);
    }
    return result;
}

}

namespace {
bool Install() {
    if(!Compatible())return false;
    if(hooked)return true;
    if(!ExchangeSite(Original,patch))return false;
    hooked=true;return true;
}
bool InstallDamage() {
    if(!Compatible())return false;
    if(damageHooked)return true;
    if(!ExchangeSite(DamageOriginal,damagePatch,DamageRva))return false;
    damageHooked=true;return true;
}
class Plugin final:public IPlugin {
    IAshitaCore* core_=nullptr;
    bool owns_=false;
    bool window_=false,dirty_=false,saveFailed_=false,compatibilityFailed_=false;
    bool damageRetry_=false;
    DWORD renderThread_=0;
    ULONGLONG nextSave_=0;
    Options options_{};
    char settingsDirectory_[MAX_PATH]{},settingsPath_[MAX_PATH]{};
    void Save(bool force=false) {
        if(!dirty_||(!force&&GetTickCount64()<nextSave_))return;
        std::error_code error;
        if(settingsPath_[0])std::filesystem::create_directories(settingsDirectory_,error);
        const bool success=settingsPath_[0]&&!error&&SaveOptions(settingsPath_,options_);
        if(success){dirty_=false;saveFailed_=false;nextSave_=0;return;}
        nextSave_=GetTickCount64()+5000;
        if(!saveFailed_&&core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Settings save failed; changes remain pending and will be retried.");
        saveFailed_=true;
    }
    void SelectMode(unsigned value) {
        options_.mode=value;requested.store(value);nameFault.store(false);
        if(!value)mode.store(0);
        dirty_=true;
    }
    void ChangedVisuals() { PublishVisuals(options_);dirty_=true; }
    void AttemptDamage(bool announce) {
        damageHookAttempted=true;
        if(InstallDamage()){
            damageFault.store(false);PublishVisuals(options_);
            if(announce&&core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage retry succeeded; adjustments %s.",options_.damageEnabled?"enabled":"off in settings");
        }else {
            damageFault.store(true);damageEnabled.store(false);
            if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage unavailable: %s. Native damage retained.",lastProblem);
        }
    }
public:
    bool HandleIncomingPacket(std::uint16_t id,std::uint32_t size,const std::uint8_t*,std::uint8_t* data,
        std::uint32_t,const std::uint8_t*,bool injected,bool blocked)override {
        if(injected||!data)return false;
        if((id==0x00A||id==0x00B)&&!blocked){
            levels.Clear();
            traitZone.store(id==0x00A&&size>=0x32?PacketValue<std::uint16_t>(data,0x30):0);
            return false;
        }
        if(id==0x00E&&!blocked&&size>=0x0B){
            if((data[0x0A]&0x20)||(size>=0x20&&(data[0x0A]&4)&&data[0x1E]==0))
                levels.Forget(PacketValue<std::uint16_t>(data,8),PacketValue<std::uint32_t>(data,4));
        }
        if(id!=0x029||size<0x1C)return false;
        const auto message=PacketValue<std::uint16_t>(data,0x18)&0x7FFF;
        if(message!=249&&(message<170||message>178))return false;
        // Check replies are addressed from the local player, not combat events
        // from another actor. Decode only the documented check message family.
        const auto actor=PacketValue<std::uint32_t>(data,4);
        if(!actor||actor!=LocalIdentity())return false;
        return levels.Result(PacketValue<std::uint16_t>(data,0x16),PacketValue<std::uint32_t>(data,8),
            PacketValue<std::uint32_t>(data,0x0C),PacketValue<std::uint32_t>(data,0x10),
            message);
    }
    bool HandleOutgoingPacket(std::uint16_t id,std::uint32_t size,const std::uint8_t*,std::uint8_t* data,
        std::uint32_t,const std::uint8_t*,bool injected,bool blocked)override {
        if(!data)return false;
        if(id==0x0DD&&size>=0x10&&data[0x0C]==0)
            return levels.Outgoing(PacketValue<std::uint16_t>(data,8),PacketValue<std::uint32_t>(data,4),injected,blocked);
        // The normal position heartbeat supplies a bounded opportunity to check
        // one selected target. No render polling, extra actor scan, or worker.
        if(id!=0x015||injected||blocked||!core_||!autoCheck.load()||!showLevels.load()||mode.load()<2||!LocalIdentity())return false;
        Ashita::FFXI::targetentry_t target;
        if(!CheckTarget(target)||!levels.Prepare(target.Index,target.ServerId,GetTickCount64()))return false;
        std::uint8_t check[0x10]{};
        std::memcpy(check+4,&target.ServerId,4);
        const auto index=static_cast<std::uint16_t>(target.Index);std::memcpy(check+8,&index,2);
        core_->GetPacketManager()->AddOutgoingPacket(0x0DD,sizeof(check),check);
        return false;
    }
    const char* GetName()const override{return "NameplateLab";}
    const char* GetAuthor()const override{return "KraturLabs";}
    const char* GetDescription()const override{return "Reloadable native nameplates with sizing, aspect correction and enemy HP color fill";}
    double GetVersion()const override{return 0.810;}
    double GetInterfaceVersion()const override{return ASHITA_INTERFACE_VERSION;}
    // Block our automatic check replies before default-priority Addons can print
    // replacement chat. Manual replies remain available to their normal handlers.
    std::int32_t GetPriority()const override{return -1;}
    std::uint32_t GetFlags()const override{return static_cast<std::uint32_t>(Ashita::PluginFlags::UseCommands|Ashita::PluginFlags::UseDirect3D|Ashita::PluginFlags::UsePackets);}
    bool Direct3DInitialize(IDirect3DDevice8* device)override{
        if(!device)return false;
        traitTextureResult=traitTexture.Initialize(device);
        if(FAILED(traitTextureResult)&&core_)
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Trait artwork unavailable (%08X); names and levels remain available.",static_cast<unsigned>(traitTextureResult));
        return true;
    }
    bool Initialize(IAshitaCore* core,ILogManager*,std::uint32_t)override{
        if(retainedModule){core->GetChatManager()->Writef(207,false,"[NameplateLab] Previous teardown could not detach safely; restart the game before loading again.");return false;}
        unsigned zero=0;
        if(!instances.compare_exchange_strong(zero,1))return false;
        owns_=true;core_=core;
        levels.Clear();
        const auto memory=core_->GetMemoryManager();
        const auto party=memory?memory->GetParty():nullptr;
        traitZone.store(party?party->GetMemberZone(0):0);
        ConfigureNative(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"FFXiMain.dll")));
        if(!Compatible()){core_->GetChatManager()->Writef(207,false,"[NameplateLab] Refused: %s.",lastProblem);return false;}
        const char* root=core_->GetInstallPath();
        if(root&&_snprintf_s(settingsDirectory_,sizeof(settingsDirectory_),_TRUNCATE,"%s\\config\\nameplatelab",root)>=0
            &&_snprintf_s(settingsPath_,sizeof(settingsPath_),_TRUNCATE,"%s\\settings.ini",settingsDirectory_)>=0)
            options_=LoadOptions(settingsPath_);
        else {settingsPath_[0]=0;saveFailed_=true;}
        cursorAttempted=false;cursorReady=false;cursorDraws=0;cursorClearance[0]={};cursorClearance[1]={};
        damageFault.store(false);nameFault.store(false);damageRetry_=false;
        PublishVisuals(options_);damageHookAttempted=false;
        damageAdjusted.store(0);damageRejected.store(0);
        mode.store(0);requested.store(options_.mode);
        replaced.store(0);rejected.store(0);drawingErrors.store(0);healthNames.store(0);filteredNames.store(0);
        core_->GetChatManager()->Writef(207,false,"[NameplateLab 0.8.1] Ready. /nplab opens nameplate settings; /nplab original restores native drawing.");
        return true;
    }
    void Release()override{
        if(!owns_)return;
        Save(true);window_=false;
        keepCursor=false;
        mode.store(0,std::memory_order_release);requested.store(0);
        damageEnabled.store(false);
        bool detached=true;
        // Native draw calls must have returned before Ashita frees this module.
        // Normal queued unload runs in Present on the rendering thread. A different
        // teardown thread cannot establish that boundary, so it must retain the code.
        if((hooked||damageHooked||cursorHooked)&&renderThread_!=GetCurrentThreadId()){
            detached=false;Problem("unload outside rendering thread");
        }else{
            if(cursorHooked){if(ExchangeSite(cursorPatch,CursorOriginal,CursorRva,5))cursorHooked=false;else detached=false;}
            if(hooked){if(ExchangeSite(patch,Original))hooked=false;else detached=false;}
            if(damageHooked){if(ExchangeSite(damagePatch,DamageOriginal,DamageRva))damageHooked=false;else detached=false;}
        }
        if(!detached){
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&NameplateGate),&retainedModule);
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Unload could not detach safely (%s). Drawing disabled; DLL retained. Restart before replacing it.",lastProblem);
        }
        if(detached)traitTexture.Release();
        core_=nullptr;owns_=false;instances.store(0);
    }
    bool HandleCommand(std::int32_t,const char* command,bool injected)override{
        (void)injected;
        if(!command||(_strnicmp(command,"/nplab",6)!=0)||(command[6]&&command[6]!=' '))return false;
        const char* option=command+6;while(*option==' ')++option;
        if(!*option||_stricmp(option,"config")==0){window_=!window_;if(!window_)Save();return true;}
        if(_stricmp(option,"self")==0){SelectMode(1);}
        else if(_stricmp(option,"all")==0){SelectMode(2);}
        else if(_stricmp(option,"hp")==0){SelectMode(3);}
        else if(_stricmp(option,"original")==0||_stricmp(option,"off")==0){SelectMode(0);}
        else if(_strnicmp(option,"traits ",7)==0){
            const auto setting=option+7;
            if(_stricmp(setting,"on")==0||_stricmp(setting,"off")==0){
                options_.showTraits=_stricmp(setting,"on")==0;ChangedVisuals();
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] traits on | off");
        }
        else if(_strnicmp(option,"levels ",7)==0||_strnicmp(option,"autocheck ",10)==0){
            const bool display=_strnicmp(option,"levels ",7)==0;
            const char* setting=option+(display?7:10);
            if(_stricmp(setting,"on")==0||_stricmp(setting,"off")==0){
                (display?options_.showLevels:options_.autoCheck)=_stricmp(setting,"on")==0;ChangedVisuals();
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] levels on | off; autocheck on | off");
        }
        else if(_strnicmp(option,"cursor ",7)==0){
            if(_stricmp(option+7,"on")==0||_stricmp(option+7,"off")==0){
                options_.keepCursor=_stricmp(option+7,"on")==0;cursorAttempted=false;ChangedVisuals();
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] cursor on | off");
        }
        else if(_strnicmp(option,"icons ",6)==0){
            const char* setting=option+6;
            if(_stricmp(setting,"show")==0||_stricmp(setting,"hide")==0){
                options_.showStatusIcons=_stricmp(setting,"show")==0;ChangedVisuals();
                if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Party / bazaar / linkshell icons: %s.",options_.showStatusIcons?"detached left; text centered independently":"hidden");
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] icons show | hide");
        }
        else if(_strnicmp(option,"damage ",7)==0){
            const char* setting=option+7;
            if(_stricmp(setting,"retry")==0){damageRetry_=true;}
            else if(_stricmp(setting,"fit")==0){
                SizingReference reference;
                if(ReadSizingReference(reference)&&ApplyDamageSizing(options_,reference.screenWidth,reference.screenHeight))ChangedVisuals();
                else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Display resolution unavailable or outside the sizing range; settings unchanged.");
            }else if(_stricmp(setting,"off")==0){options_.damageEnabled=false;ChangedVisuals();}
            else if(_stricmp(setting,"on")==0){options_.damageEnabled=true;ChangedVisuals();}
            else if(_strnicmp(setting,"size ",5)==0||_strnicmp(setting,"width ",6)==0){
                const bool size=_strnicmp(setting,"size ",5)==0;float value=0;
                if(ParseFactor(setting+(size?5:6),value)){
                    if(size)options_.damageScale=value;else options_.damageWidth=value;
                    options_.damageEnabled=true;ChangedVisuals();
                }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage size/width requires a factor from 0.25 to 3.");
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] damage fit | size <factor> | width <factor> | on | off | retry");
        }
        else if(_stricmp(option,"fit")==0){
            SizingReference reference;
            if(ReadSizingReference(reference)&&ApplyOriginalSizing(options_,reference.screenWidth,reference.screenHeight)){
                ChangedVisuals();
                if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Matched original 4:3 proportions for %ux%u: size 100%%, width %.2f%%. HP/display and filtering retained.",reference.screenWidth,reference.screenHeight,options_.width*100);
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Cannot calculate 4:3 sizing from the current resolution; settings unchanged.");
        }
        else if(_stricmp(option,"reset")==0){options_=Options{};cursorAttempted=false;ChangedVisuals();SelectMode(options_.mode);}
        else if(_strnicmp(option,"size ",5)==0||_strnicmp(option,"width ",6)==0){
            const bool size=_strnicmp(option,"size ",5)==0;float value=0;
            if(ParseFactor(option+(size?5:6),value)){
                if(size)options_.scale=value;else options_.width=value;
                ChangedVisuals();
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Use a factor from 0.25 to 3, for example /nplab size 1.2 or /nplab width 0.85.");
        }
        else if(_stricmp(option,"status")==0){
            core_->GetChatManager()->Writef(207,false,"[NameplateLab 0.7.2] %s; size %.0f%% / width %.0f%%; widescreen %s; %s filtering; recreated %u names; HP-colored %u; fallbacks %u; drawing errors %u.",mode.load()==1?"Self":mode.load()==2?"All":mode.load()==3?"All + enemy HP":nameFault.load()?"Suspended (drawing error)":"Original",options_.scale*100,options_.width*100,options_.correctAspect?"corrected":"native",options_.filter==1?"sharp":options_.filter==2?"smooth":"native",replaced.load(),healthNames.load(),rejected.load(),drawingErrors.load());
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Party / bazaar / linkshell icons: %s.",options_.showStatusIcons?"detached left":"hidden");
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage %s; size %.0f%% / width %.2f%%; adjusted %u; rejected %u.",damageFault.load()?"unavailable (use /nplab damage retry)":damageHooked&&damageEnabled.load()?"enabled":options_.damageEnabled?"pending":"native",options_.damageScale*100,options_.damageWidth*100,damageAdjusted.load(),damageRejected.load());
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Cursor %s; forced native draws %u.",!options_.keepCursor?"off":!cursorAttempted?"pending":cursorReady&&keepCursor?"enabled":cursorProblem,cursorDraws);
            if(drawingErrors.load())core_->GetChatManager()->Writef(207,false,"[NameplateLab] Last drawing error: %s failed (HRESULT 0x%08X), %u quads submitted in that name. Select a display mode to retry.",lastDrawFailure.operation,static_cast<unsigned>(lastDrawFailure.error),lastDrawFailure.submitted);
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Intentionally filtered names: %u.",filteredNames.load());
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Private glyph submission; shared entry %s.",submissionDetoured.load()?"detoured (left unchanged)":"native");
        }else core_->GetChatManager()->Writef(207,false,"[NameplateLab] /nplab (settings) | self | all | hp | original | size <factor> | width <factor> | fit | icons show|hide | cursor on|off | levels on|off | autocheck on|off | traits on|off | damage <setting> | reset | status");
        Save();
        return true;
    }
    void Direct3DPresent(const RECT*,const RECT*,HWND,const RGNDATA*)override{
        if(!window_||!core_)return;
        auto* gui=core_->GetGuiManager();
        if(!gui||!gui->GetCurrentContext())return;
        gui->SetNextWindowSize(ImVec2(430,0),ImGuiCond_FirstUseEver);
        if(gui->Begin("NameplateLab",&window_,ImGuiWindowFlags_AlwaysAutoResize)){
            bool changed=false;
            if(gui->BeginTabBar("SettingsTabs")){
            if(gui->BeginTabItem("General")){
            gui->SeparatorText("Nameplates");
            int selected=static_cast<int>(requested.load());
            if(gui->Combo("Display",&selected,"Original\0Self only\0All names\0All names + enemy HP\0"))SelectMode(static_cast<unsigned>(selected));
            float size=options_.scale*100,width=options_.width*100;
            if(gui->SliderFloat("Size",&size,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.scale=size/100;changed=true;}
            if(gui->SliderFloat("Width",&width,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.width=width/100;changed=true;}
            int filter=static_cast<int>(options_.filter);
            if(gui->Combo("Rendering",&filter,"Native\0Sharp\0Smooth\0")){options_.filter=static_cast<unsigned>(filter);changed=true;}
            changed=gui->Checkbox("Show party / bazaar / linkshell icons",&options_.showStatusIcons)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Independent icons to the left, in that order. Active icons pack toward the name; text stays centered.");
            SizingReference reference;
            const bool canFit=ReadSizingReference(reference);
            gui->BeginDisabled(!canFit);
            if(gui->Button("Match original 4:3")&&canFit){
                changed=ApplyOriginalSizing(options_,reference.screenWidth,reference.screenHeight)||changed;
            }
            gui->EndDisabled();
            if(canFit){
                gui->Text("%ux%u baseline: Size 100%% / Width %.2f%%",reference.screenWidth,reference.screenHeight,reference.width*100);
                gui->TextUnformatted("Matches 4:3 screen proportions; keeps native height.");
            }else gui->TextUnformatted("Resolution unavailable or outside the sizing range.");
            if(gui->Button("Reset appearance")){
                options_.scale=1;options_.width=1;options_.correctAspect=false;options_.filter=2;options_.showStatusIcons=true;changed=true;
            }
            gui->SeparatorText("Target cursor");
            if(gui->Checkbox("Keep native overhead cursor",&options_.keepCursor)){cursorAttempted=false;changed=true;}
            if(gui->IsItemHovered())gui->SetTooltip("Keeps the native animated arrow when the target panel is hidden. Leaves the panel hidden.");
            if(options_.keepCursor&&!cursorReady)gui->Text("Cursor: %s",cursorProblem);
            gui->SeparatorText("Damage numbers");
            changed=gui->Checkbox("Adjust damage numbers",&options_.damageEnabled)||changed;
            if(damageFault.load()){
                gui->TextUnformatted("Damage adjustments unavailable. Retry checks compatibility again.");
                if(gui->Button("Retry damage adjustments"))damageRetry_=true;
            }
            float damageSize=options_.damageScale*100,damageWide=options_.damageWidth*100;
            if(gui->SliderFloat("Size##damage",&damageSize,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.damageScale=damageSize/100;options_.damageEnabled=true;changed=true;}
            if(gui->SliderFloat("Width##damage",&damageWide,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.damageWidth=damageWide/100;options_.damageEnabled=true;changed=true;}
            gui->BeginDisabled(!canFit);
            if(gui->Button("Match original 4:3##damage")&&canFit)
                changed=ApplyDamageSizing(options_,reference.screenWidth,reference.screenHeight)||changed;
            gui->EndDisabled();
            if(canFit)gui->Text("Baseline: Size 100%% / Width %.2f%%",reference.width*100);
            else gui->TextUnformatted("Display resolution unavailable or outside the sizing range.");
            gui->TextUnformatted("Keeps the game's damage animation, colors and font.");
            if(gui->Button("Reset damage appearance")){
                options_.damageEnabled=false;options_.damageCorrectAspect=false;
                options_.damageScale=1;options_.damageWidth=1;changed=true;
            }
            gui->SeparatorText("Nameplate font");
            gui->TextUnformatted("Uses the game's loaded font, including XIPivot DATs.");
            gui->TextUnformatted("Sharp keeps pixel edges; Smooth softens scaling.");
            gui->TextUnformatted("Size and width also apply to nameplate icons.");
            gui->EndTabItem();
            }
            if(gui->BeginTabItem("Details")){
            gui->SeparatorText("Monster levels");
            changed=gui->Checkbox("Show checked levels",&options_.showLevels)||changed;
            changed=gui->Checkbox("Only on current target##levels",&options_.levelsTargetOnly)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Hides other level labels without clearing learned levels. Checking continues normally.");
            changed=gui->Checkbox("Automatically check monster targets",&options_.autoCheck)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Silent checks while levels are shown. Manual /check still prints normally. Unknown levels stay hidden.");
            float levelSize=options_.levelScale*100;
            if(gui->SliderFloat("Level size",&levelSize,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){
                options_.levelScale=levelSize/100;changed=true;
            }
            if(gui->IsItemHovered())gui->SetTooltip("Scales only the level label relative to the name. Changes appear immediately.");
            if(gui->Button("Reset level size")){options_.levelScale=1;changed=true;}
            gui->SeparatorText("Monster traits");
            changed=gui->Checkbox("Show detection / linking / aggression",&options_.showTraits)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("MobDB defaults, not current hostility. Red: aggressive. Blue: passive. Unknown traits stay hidden; server behavior may differ.");
            changed=gui->Checkbox("Only on current target##traits",&options_.traitsTargetOnly)||changed;
            float traitSize=options_.traitScale*100;
            if(gui->SliderFloat("Trait size",&traitSize,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){
                options_.traitScale=traitSize/100;changed=true;
            }
            if(gui->Button("Reset trait size")){options_.traitScale=1;changed=true;}
            gui->TextUnformatted("True sight / sight / sound / magic / job ability / blood / link");
            gui->TextUnformatted("Red: aggressive   Blue: passive   Missing data: hidden");
            if(FAILED(traitTextureResult))gui->TextUnformatted("Trait artwork unavailable; reload the plugin to try again.");
            gui->EndTabItem();
            }
            gui->EndTabBar();
            }
            if(changed)ChangedVisuals();
            if(nameFault.load())gui->TextUnformatted("A drawing error disabled replacement. See /nplab status.");
            if(saveFailed_)gui->TextUnformatted("Settings save pending; retrying while loaded.");
        }
        const bool editing=gui->IsAnyItemActive();
        gui->End();
        if(!window_||!editing)Save();
    }
    void Direct3DBeginScene(bool)override{
        // Initialize may run on the loading thread. Capture the actual drawing
        // thread before installing hooks, not the device initialization caller.
        if(!renderThread_)renderThread_=GetCurrentThreadId();
        RefreshDetailTarget();
        cursorClearance[0]={};cursorClearance[1]={};
        if(cursorHooked){
            unsigned char bytes[5];
            if(!ReadBytes(clientBase+CursorRva,bytes,5)||std::memcmp(bytes,cursorPatch,5)){
                keepCursor=false;cursorReady=false;cursorAttempted=true;strcpy_s(cursorProblem,"cursor hook ownership changed");
            }
        }
        if(!keepCursor&&!cursorAttempted){
            cursorAttempted=true;
            if(cursorHooked){
                if(ExchangeSite(cursorPatch,CursorOriginal,CursorRva,5)){cursorHooked=false;cursorReady=false;}
                else strcpy_s(cursorProblem,lastProblem);
            }
        }
        if(keepCursor&&!cursorAttempted){
            cursorAttempted=true;
            if(!InstallCursor()){cursorReady=false;keepCursor=false;if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Cursor unavailable: %s",cursorProblem);}
        }
        if(saveFailed_)Save();
        if((mode.load()||damageHooked)&&!OwnsHooks()){
            damageEnabled.store(false);damageFault.store(true);damageRetry_=false;
            requested.store(0);mode.store(0);
            if(!compatibilityFailed_&&core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Replacement disabled: %s.",lastProblem);
            compatibilityFailed_=true;return;
        }
        compatibilityFailed_=false;
        if(DrawingDeviceRecovered()){nameFault.store(false);requested.store(options_.mode);}
        if(damageRetry_){
            damageRetry_=false;AttemptDamage(true);
        }else if(options_.damageEnabled&&!damageHooked&&!damageHookAttempted){
            AttemptDamage(false);
        }
        const unsigned next=requested.load();
        if(next==mode.load())return;
        if(!next){mode.store(0);return;}
        if(!Install()){
            requested.store(0);mode.store(0);
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Original retained: %s.",lastProblem);return;
        }
        mode.store(next,std::memory_order_release);
        core_->GetChatManager()->Writef(207,false,"[NameplateLab] %s enabled. /nplab original switches back.",next==1?"Self recreation":next==3?"Enemy HP coloring":"All-name recreation");
    }
};
}

extern "C" IPlugin* __stdcall CreatePlugin(const char*){return new(std::nothrow) Plugin();}
extern "C" void __stdcall DestroyPlugin(void* instance){auto* plugin=static_cast<Plugin*>(instance);if(plugin){plugin->Release();delete plugin;}}
extern "C" double __stdcall InterfaceVersion(){return ASHITA_INTERFACE_VERSION;}

namespace {
void ConfigureNative(std::uintptr_t base) {
    clientBase=base;
    const auto nameGateAddress=reinterpret_cast<std::uintptr_t>(&NameplateGate);
    const auto damageGateAddress=reinterpret_cast<std::uintptr_t>(&DamageGate);
    const auto cursorGateAddress=reinterpret_cast<std::uintptr_t>(&CursorMenuGate);
    NameplateResume=base+native_contract::NameResume;NameplateExit=base+native_contract::NameExit;
    DamageResume=base+native_contract::DamageResume;
    MenuDrawAddress=base+native_contract::MenuDraw;TargetWindowAddress=base+0x578478;
    CursorTailAddress=base+0x1504B6;NameplateSubmitResume=base+SubmitRva+SubmitPrefixSize;
    patch[0]=0xE9;patch[5]=0x90;
    damagePatch[0]=0xE9;damagePatch[5]=0x90;cursorPatch[0]=0xE8;
    const auto nameOffset=static_cast<std::uint32_t>(nameGateAddress-(base+HookRva+5));
    const auto damageOffset=static_cast<std::uint32_t>(damageGateAddress-(base+DamageRva+5));
    const auto cursorOffset=static_cast<std::uint32_t>(cursorGateAddress-(base+CursorRva+5));
    std::memcpy(patch+1,&nameOffset,4);std::memcpy(damagePatch+1,&damageOffset,4);std::memcpy(cursorPatch+1,&cursorOffset,4);
}
}
extern "C" {
unsigned __stdcall RenderDamage(std::uintptr_t frame) noexcept {return AdjustDamage(frame);}
void __fastcall CursorMenuGate(std::uintptr_t menu,void*) noexcept {
    if(*reinterpret_cast<const std::uint32_t*>(menu+0xC)==*reinterpret_cast<const std::uint32_t*>(TargetWindowAddress)
        &&RenderCursorMenu(menu))return;
    reinterpret_cast<void(__thiscall*)(std::uintptr_t)>(MenuDrawAddress)(menu);
}
#include "native_gate.h"
}
