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
#include "actions.h"
#include "xp.h"
#include "traits.h"
#include "trait_texture.h"
#include "debuff_texture.h"
#include "options.h"
#include "native_discovery.h"
#include <memory>

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
constexpr char Version[]="0.9.19";
// Native icon expansion tables; setup requires exactly these values before any hook.
constexpr unsigned char ExpansionBase[6]={169,169,169,169,169,169},ExpansionCount[6]={0,1,2,0,1,2};
native_discovery::Addresses native;
std::uint32_t HookRva=0;
std::uint32_t SubmitRva=0;
constexpr std::uint32_t SubmitPrefixSize=7;
std::atomic<bool> submissionDetoured{false};
constexpr unsigned char Original[6]={0x33,0xD2,0x33,0xED,0x33,0xF6};
std::uintptr_t clientBase=0;
std::uint32_t DamageRva=0;
constexpr unsigned char DamageOriginal[6]={0x81,0xEC,0x24,0x03,0x00,0x00};
unsigned char damagePatch[6]{};
bool damageHooked=false,damageHookAttempted=false;
std::uint32_t CursorRva=0;
unsigned char CursorOriginal[5]{};
unsigned char cursorPatch[5]{};
bool cursorHooked=false,cursorAttempted=false,cursorReady=false;
std::atomic<bool> keepCursor{false},hideTarget{false};
// The target window's menu holds its visibility: +0x69 frame, +0x6A draw callback.
// While hiding, (0,0) is our own state; prior is what the game or another addon left.
struct HiddenTarget { std::uintptr_t menu=0; unsigned char prior[2]{}; } hiddenTarget;
HMODULE retainedModule=nullptr; // Only a failed teardown retains code; normal unload frees the DLL.
void ConfigureNative(std::uintptr_t base);
bool DiscoverNative(std::uintptr_t base);
CursorClearance cursorClearance[2]; // Only current-scene, actually drawn overhead content.


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
std::atomic<bool> autoCheck{true};
struct DetailTarget { unsigned index=0;std::uint32_t id=0; } detailTarget;
DetailTarget sceneCursorTargets[2];
Debuffs debuffs;
DebuffTexture debuffTexture;
HRESULT debuffTextureResult=S_OK;
std::atomic<bool> previewDebuffs{false};
std::atomic<unsigned> rowFeatures[RowCount]; // Saved per-category detail columns.
std::atomic<unsigned> rowFront[RowCount]; // Parts each category draws on top.
std::atomic<bool> unclaimedDamagedOnly{false},friendlyHealth{false},npcFeatures{false};
// Whether any shown row enables this detail column.
bool AnyRows(unsigned column) noexcept {
    for(const auto& row:rowFeatures){const auto value=row.load(std::memory_order_relaxed);if((value&RowShow)&&(value&column))return true;}
    return false;
}
// Your party/alliance roster for the current scene: identities for categories
// and claims, TP/MP and status debuffs. Numeric only; no native pointers.
struct SceneMember { DetailTarget identity; DebuffRow row; unsigned tp=0; int mp=-1; };
SceneMember sceneMembers[18];
DebuffRow StatusDebuffs(const std::int16_t* statuses) noexcept {
    DebuffRow row;
    if(statuses)for(unsigned i=0;i<32;++i)
        if(DebuffCell(static_cast<unsigned>(statuses[i]))!=~0u)row.effects[row.count++]=static_cast<std::uint16_t>(statuses[i]);
    return row;
}
DebuffRow StatusDebuffs(const std::uint8_t* statuses,std::uint64_t mask) noexcept {
    std::int16_t decoded[32];
    if(!statuses)return {};
    for(unsigned i=0;i<32;++i)decoded[i]=static_cast<std::int16_t>(statuses[i]|(((mask>>(i*2))&3)<<8));
    return StatusDebuffs(decoded);
}
std::atomic<unsigned> debuffSize{16};
std::uint32_t sceneSeconds=0,sceneMillis=0;
XpFeed xpFeed;
std::atomic<bool> scrollXp{false};
std::atomic<bool> growTarget{false};
std::atomic<float> growFarSize{1};
Actions actions;
// Fixed sizes: level 60%, distance 45% of the name. Traits follow their slider.
constexpr float LevelScale=.6f,LabelScale=.45f;
std::atomic<float> actionScale{.6f},weakScale{1},resistScale{1},traitScale{.8f};
Levels levels;
std::atomic<unsigned> traitZone{0}; // Initial SDK zone, then zone-transition packets.
TraitTexture traitTexture;
HRESULT traitTextureResult=S_OK;
void PublishVisuals(const Options& options) noexcept {
    keepCursor=options.keepCursor;hideTarget=options.hideTarget;
    autoCheck=options.autoCheck;debuffSize=options.debuffSize;
    for(unsigned row=0;row<RowCount;++row){rowFeatures[row]=options.rows[row];rowFront[row]=options.front[row];}
    unclaimedDamagedOnly=options.unclaimedDamagedOnly;friendlyHealth=options.friendlyHealth;npcFeatures=options.npcFeatures;
    actionScale=options.actionScale;weakScale=options.weakScale;resistScale=options.resistScale;traitScale=options.traitScale;scrollXp=options.scrollXp;
    growTarget=options.growTarget;growFarSize=options.growFarSize;
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
static_assert(offsetof(Ashita::FFXI::entity_t, Distance) == 0xD8); // Squared yalms.
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
    if(Read(clientBase+native.playerIndex,index)&&index&&index<0x900
        &&Read(clientBase+native.entities+index*4,entity)&&entity)Read(entity+0x78,id);
    return id;
}
bool CheckTarget(Ashita::FFXI::targetentry_t& target) noexcept {
    std::uint32_t controller=0,id=0,entity=0,flags=0;std::uint8_t hp=0;
    return Read(clientBase+native.targets,controller)&&controller
        &&Read(controller,target)&&target.IsActive&&target.ServerId&&target.Index<0x900
        &&Read(clientBase+native.entities+target.Index*4,entity)&&entity&&entity==target.EntityPointer
        &&Read(entity+0x78,id)&&id==target.ServerId
        &&Read(entity+offsetof(Ashita::FFXI::entity_t,SpawnFlags),flags)
        &&(flags&EnemyFlag)&&!(flags&FriendlyFlags)
        &&Read(entity+offsetof(Ashita::FFXI::entity_t,HPPercent),hp)&&hp>0&&hp<=100;
}
void RefreshDetailTarget() noexcept {
    // One current-scene identity for both filters. Collection already validates
    // each visible entity, so do not repeat entity/HP reads or cache pointers.
    detailTarget={};
    sceneCursorTargets[0]={};sceneCursorTargets[1]={};
    const bool debuffCursor=(AnyRows(ShowDebuffs)||previewDebuffs.load())&&debuffTexture.value&&keepCursor.load();
    if(!requested.load())return;
    std::uint32_t controller=0;
    Ashita::FFXI::targetentry_t target;
    if(Read(clientBase+native.targets,controller)&&controller&&Read(controller,target)&&target.IsActive)
        detailTarget={target.Index,target.ServerId};
    if(debuffCursor&&controller){
        sceneCursorTargets[0]=detailTarget;
        if(Read(controller+sizeof(target),target)&&target.IsActive)sceneCursorTargets[1]={target.Index,target.ServerId};
    }
}
struct CombatContext { std::uint32_t now; IParty* party; IResourceManager* resources; };
// Jobs with MP: WHM BLM RDM PLD DRK SMN BLU SCH GEO RUN.
bool MagicJob(unsigned job) noexcept {
    return job==3||job==4||job==5||job==7||job==8||job==15||job==16||job==20||job==21||job==22;
}
void RefreshScene(IMemoryManager* memory) noexcept {
    for(auto& entry:sceneMembers)entry={};
    auto* party=memory?memory->GetParty():nullptr;
    if(!party)return;
    // Identities always feed categories and claims; TP, MP and statuses only when shown.
    const bool statuses=AnyRows(ShowDebuffs),tp=AnyRows(ShowTp),mp=AnyRows(ShowMp);
    const auto zone=party->GetMemberZone(0);
    for(unsigned member=0;member<18;++member){
        if(!party->GetMemberIsActive(member)||party->GetMemberZone(member)!=zone)continue;
        auto& entry=sceneMembers[member];
        entry.identity={party->GetMemberTargetIndex(member),party->GetMemberServerId(member)};
        if(tp)entry.tp=party->GetMemberTP(member);
        if(member==0){
            // Your own maximum MP is exact, including a support job too low for MP.
            auto* player=mp||statuses?memory->GetPlayer():nullptr;
            if(mp&&player&&player->GetMPMax())entry.mp=party->GetMemberMPPercent(0);
            if(player&&statuses)entry.row=StatusDebuffs(player->GetBuffs());
            continue;
        }
        if(mp&&(MagicJob(party->GetMemberMainJob(member))||MagicJob(party->GetMemberSubJob(member))))
            entry.mp=party->GetMemberMPPercent(member);
        if(!statuses)continue;
        if(member<6){
            // Status entries are keyed by identity, not party roster order.
            for(unsigned status=0;status<5;++status)
                if(party->GetStatusIconsServerId(status)==entry.identity.id
                    &&party->GetStatusIconsTargetIndex(status)==entry.identity.index){
                    entry.row=StatusDebuffs(party->GetStatusIcons(status),party->GetStatusIconsBitMask(status));break;
                }
        }else entry.row=debuffs.Read(entry.identity.index,entry.identity.id,sceneSeconds);
    }
}
// Monster IDs encode the local index. Player IDs must be resolved through the
// bounded roster from firstMember, not their low ID bits. 0x900: not current.
unsigned ObservedIndex(std::uint32_t id,IParty* party,unsigned firstMember,bool rosterPlayers) noexcept {
    const auto matches=[&](unsigned slot,bool roster){
        std::uint32_t entity=0,current=0,flags=0;
        return slot<0x900&&Read(clientBase+native.entities+slot*4,entity)&&entity
            &&Read(entity+0x78,current)&&current==id
            &&Read(entity+offsetof(Ashita::FFXI::entity_t,SpawnFlags),flags)
            &&(roster?!rosterPlayers||(flags&1)!=0:((flags&EnemyFlag)&&!(flags&FriendlyFlags)));
    };
    if(matches(id&0xFFF,false))return id&0xFFF;
    if(party)for(unsigned member=firstMember;member<18;++member)
        if(party->GetMemberIsActive(member)&&party->GetMemberServerId(member)==id
            &&party->GetMemberZone(member)==party->GetMemberZone(0)){
            const unsigned index=party->GetMemberTargetIndex(member);
            return matches(index,true)?index:0x900;
        }
    return 0x900;
}
void ReceiveDebuff(void* context,const DebuffEvent& event) {
    const auto& state=*static_cast<CombatContext*>(context);
    // Only alliance lacks an authoritative status list.
    const auto index=ObservedIndex(event.target,state.party,6,true);
    if(index<0x900)debuffs.Apply(index,event.target,event.change,event.effect,state.now,event.rank,event.duration,event.preserveLonger);
}
// Enemies, you, party and alliance (including trusts). Names resolve once, here.
void ReceiveAction(void* context,const ActionEvent& event) {
    const auto& state=*static_cast<CombatContext*>(context);
    const auto index=ObservedIndex(event.actor,state.party,0,false);
    if(index>=0x900)return;
    if(event.change==ActionChange::Clear){actions.Forget(index,event.actor);return;}
    if(event.change==ActionChange::Finish){actions.Finish(index,event.actor,event.result,state.now);return;}
    const char* name=nullptr;
    if(auto* resources=state.resources){
        const bool instant=event.change==ActionChange::Instant;
        if(event.category==8){if(const auto* spell=resources->GetSpellById(event.id))name=spell->Name[0];}
        // Completed job abilities use the header ID plus the resource-table offset.
        else if(instant||event.actor<0x1000000||event.id<256){
            if(const auto* ability=resources->GetAbilityById(instant?event.id+512:event.id))name=ability->Name[0];
        }else name=resources->GetString("monsters.abilities",event.id-256);
        actions.Show(index,event.actor,name,event.result,instant,state.now);
    }
}
// Detail column named by a "<name> on|off" command, or 0.
unsigned DetailCommand(const char* option) noexcept {
    static constexpr struct {const char* name;unsigned column;} Commands[]={{"levels",ShowLevel},{"traits",ShowTraits},
        {"debuffs",ShowDebuffs},{"health",ShowHealth},{"mp",ShowMp},{"tp",ShowTp},{"distance",ShowDistance},{"actions",ShowAction},{"weakness",ShowWeak},{"resistance",ShowResist}};
    const char* space=std::strchr(option,' ');
    if(!space)return 0;
    for(const auto& command:Commands)
        if(std::strlen(command.name)==static_cast<std::size_t>(space-option)&&_strnicmp(option,command.name,space-option)==0)return command.column;
    return 0;
}
std::uintptr_t TargetMenu() noexcept {
    std::uint32_t window=0,menu=0,back=0;
    return Read(clientBase+native.targetWindow,window)&&window&&Read(window+8,menu)&&menu
        &&Read(menu+0xC,back)&&back==window?menu:0;
}
bool WriteBytes(std::uintptr_t address,const void* input,std::size_t size) noexcept {
    __try{std::memcpy(reinterpret_cast<void*>(address),input,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Runs before each scene, after any Present-time writer such as HideParty.
// Restores only through the same menu and only over our own (0,0).
void HideTargetWindow(bool hide) noexcept {
    const auto menu=TargetMenu();
    unsigned char flags[2];
    if(!menu||!ReadBytes(menu+0x69,flags,2)){hiddenTarget={};return;}
    if(!hide){
        if(hiddenTarget.menu==menu&&!flags[0]&&!flags[1])WriteBytes(menu+0x69,hiddenTarget.prior,2);
        hiddenTarget={};return;
    }
    if(menu!=hiddenTarget.menu||flags[0]||flags[1]){hiddenTarget.menu=menu;std::memcpy(hiddenTarget.prior,flags,2);}
    if(flags[0]||flags[1]){const unsigned char hidden[2]{};WriteBytes(menu+0x69,hidden,2);}
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

bool CheckNativeRange(unsigned index) noexcept {
    const auto& pattern=native_discovery::patterns[index];
    unsigned char actual[2048];
    const auto rva=native.routines[index];
    if(pattern.size>sizeof(actual)||!ReadBytes(clientBase+rva,actual,pattern.size))return false;
    if(hooked&&index==0){if(std::memcmp(actual+HookRva-rva,patch,6))return false;std::memcpy(actual+HookRva-rva,Original,6);}
    if(damageHooked&&index==4){if(std::memcmp(actual,damagePatch,6))return false;std::memcpy(actual,DamageOriginal,6);}
    if(cursorHooked&&index==6){if(std::memcmp(actual+CursorRva-rva,cursorPatch,5))return false;std::memcpy(actual+CursorRva-rva,CursorOriginal,5);}
    if(index==2){
        const bool original=std::memcmp(actual,pattern.bytes,SubmitPrefixSize)==0;
        if(!original&&!(actual[0]==0xE9&&actual[5]==0x90&&actual[6]==0x90))return false;
        submissionDetoured.store(!original,std::memory_order_relaxed);
    }
    if(index==7&&actual[0]!=0xE9&&std::memcmp(actual,pattern.bytes,5))return false;
    return native_discovery::Matches(actual,pattern);
}
bool Compatible() noexcept {
    if(!clientBase||!HookRva)return Problem("native routine discovery failed or ambiguous");
    for(unsigned i=0;i<6;++i)if(!CheckNativeRange(i))return Problem("unsupported or modified native rendering contract");
    unsigned char bytes[6];
    if(!ReadBytes(clientBase+native.expansionBase,bytes,6)||std::memcmp(bytes,ExpansionBase,6)
        ||!ReadBytes(clientBase+native.expansionCount,bytes,6)||std::memcmp(bytes,ExpansionCount,6))
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
    for(unsigned i=6;i<13;++i)if(!CheckNativeRange(i))return fail("unsupported or modified native cursor contract");
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
        ||!Read(clientBase+native.targets,controller)||!controller
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
    if(!Read(clientBase+native.modal,modal)||!modal||!Read(modal+0x48,modalActive))return 0;
    if(modalActive&&(!Read(modal+8,modalMenu)||!Read(menu+0x34,menuFlags)
        ||(modalMenu!=menu&&!(menuFlags&0x40))))return 0;
    const auto shape=state.m_pAnkShape[state.m_AnkNum];
    if(!shape)return 0; // Native resources can be absent during window teardown.
    std::int16_t bottom=0;
    if(hasClearance&&!Read(shape+0x26,bottom))return 0;
    float uiScaleX=1,uiScaleY=1;
    if(hasClearance){
        // Name vertices use render-buffer pixels; cursor anchors use UI units.
        // Read the current dimensions here so live UI/resolution changes agree.
        std::uint32_t config=0;std::uint16_t dimensions[6];
        if(!Read(clientBase+native.config,config)||!config||!ReadBytes(config+0x10,dimensions,sizeof(dimensions))
            ||!dimensions[2]||!dimensions[3]||!dimensions[4]||!dimensions[5])return 0;
        uiScaleX=static_cast<float>(dimensions[2])/dimensions[4];
        uiScaleY=static_cast<float>(dimensions[3])/dimensions[5];
    }
    const unsigned main=state.m_Sub?1u:0u;
    const int mainLift=cursorClearance[main].Lift(targets[main].ServerId,state.m_AnkY+bottom-0.5f,state.m_AnkX,uiScaleX,uiScaleY);
    const int subLift=state.m_Sub?cursorClearance[0].Lift(targets[0].ServerId,state.m_SubAnkY+bottom-0.5f,state.m_SubAnkX,uiScaleX,uiScaleY):0;
    // Temporary coordinates affect only this call. Native animation, position
    // updates, resources and main/subtarget colors remain the client's own.
    auto* live=reinterpret_cast<Ashita::FFXI::targetwindow_t*>(window);
    if(mainLift)live->m_AnkY=static_cast<std::int16_t>((std::max)(-32768,static_cast<int>(state.m_AnkY)-mainLift));
    if(subLift)live->m_SubAnkY=static_cast<std::int16_t>((std::max)(-32768,static_cast<int>(state.m_SubAnkY)-subLift));
    reinterpret_cast<void(__thiscall*)(std::uintptr_t)>(clientBase+native.menuDraw)(menu);
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
    if(!Read(frame,caller)||caller!=clientBase+native.damageCaller||!Read(frame+16,pair)
        ||pair!=frame+0x38||!ReadBytes(pair,scales,sizeof(scales))
        ||!std::isfinite(scales[0])||!std::isfinite(scales[1])
        ||scales[0]<=0||scales[1]<=0||scales[0]>128||scales[1]>128){
        damageRejected.fetch_add(1);return 0;
    }
    if(!damageEnabled.load())return 0;

    Options options;options.scale=damageScale.load();options.width=damageWidth.load();
    options.correctAspect=damageCorrectAspect.load();
    if(!SizeScale(scales[0],scales[1],options)){damageRejected.fetch_add(1);return 0;}
    // The pointer is proven to be this native call's stack slot, read just above.
    auto* output=reinterpret_cast<float*>(pair);
    output[0]=scales[0];output[1]=scales[1];
    damageAdjusted.fetch_add(1);return 1;
}

struct Resources {
    void* graphics; IDirect3DDevice8* device; IDirect3DBaseTexture8* textures[4];
    unsigned healthPercent = 100;
    std::uint32_t healthTint = 0;
    LevelLabel level;
    TraitLabel traits;
    DebuffRow debuffs;
    SideLabels labels;
    DetailTarget identity;
    float grow = 1; // Selected target's distance enlargement.
};
bool Collect(std::uintptr_t frame,Input& input,Resources& resources,StatusIcons* icons=nullptr,bool showIcons=true) noexcept {
    if(icons)*icons={};
    std::uint32_t caller=0,actor=0,entity=0,identity=0,current=0;
    if(!Read(frame+0x6D4,caller)||caller!=clientBase+native.nameCaller || !Read(frame+4,actor)
        ||!Read(actor+0x70,entity)||!Read(entity+0x78,identity)||!identity)return false;
    std::uint16_t entityIndex=0;
    if(!Read(entity+0x74,entityIndex)||entityIndex>=0x900
        ||!Read(clientBase+native.entities+entityIndex*4,current)||current!=entity)return false;
    if(mode.load(std::memory_order_relaxed)==1){
        std::uint16_t playerIndex=0;
        if(!Read(clientBase+native.playerIndex,playerIndex)||!playerIndex)return false;
        if(entityIndex!=playerIndex){intentionallyFiltered=true;return false;}
    }
    std::uint32_t spawnFlags=0;
    if(!Read(entity+offsetof(Ashita::FFXI::entity_t,SpawnFlags),spawnFlags))return false;
    const bool enemy=(spawnFlags&EnemyFlag)&&!(spawnFlags&FriendlyFlags);
    std::uint8_t hp=0;
    const bool hpKnown=Read(entity+offsetof(Ashita::FFXI::entity_t,HPPercent),hp)&&hp<=100;
    if(mode.load(std::memory_order_relaxed)==3&&enemy&&hpKnown)resources.healthPercent=hp;
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
    resources.identity={entityIndex,identity};
    unsigned member=18;
    for(unsigned m=0;m<18;++m)
        if(sceneMembers[m].identity.id==identity&&sceneMembers[m].identity.index==entityIndex){member=m;break;}
    // NPCs (not players, monsters or trusts in your party) get no details, even targeted.
    const bool plainNpc=!(spawnFlags&1)&&!enemy&&member==18&&!npcFeatures.load(std::memory_order_relaxed);
    // The first matching category decides this name's details. Other players and
    // NPCs match none unless selected. You keep your own row even when targeted.
    unsigned row=RowCount;
    if(member==0)row=RowSelf;
    else if(selected&&!plainNpc)row=RowTarget;
    else if(member<18)row=RowParty;
    else if(enemy){
        std::uint32_t claim=0;
        if(Read(entity+offsetof(Ashita::FFXI::entity_t,ClaimStatus),claim)){
            // The low word holds the claimer's server ID, truncated to 16 bits.
            const auto owner=claim&0xFFFF;
            if(!owner)row=unclaimedDamagedOnly.load(std::memory_order_relaxed)&&(!hpKnown||hp>=100)?RowCount:RowUnclaimed;
            else if(sceneMembers[0].identity.id&&owner==(sceneMembers[0].identity.id&0xFFFF))row=RowClaimedSelf;
            else{
                row=RowClaimedOther;
                for(unsigned m=1;m<18;++m)
                    if(sceneMembers[m].identity.id&&owner==(sceneMembers[m].identity.id&0xFFFF)){row=RowClaimedParty;break;}
            }
        }
    }
    unsigned features=0;
    if(singleLine&&row<RowCount){
        features=rowFeatures[row].load(std::memory_order_relaxed);
        if(!(features&RowShow))features=0;
    }
    if(row<RowCount)resources.labels.front=rowFront[row].load(std::memory_order_relaxed);
    // Preview substitutes sample debuffs and an action on the selected target
    // (enemy or player) and on you, regardless of the table. Not saved.
    bool preview=false;
    if(previewDebuffs.load(std::memory_order_relaxed)&&singleLine){
        std::uint16_t playerIndex=0;
        preview=(selected&&!plainNpc)||(Read(clientBase+native.playerIndex,playerIndex)&&playerIndex==entityIndex);
    }
    if(preview){if(debuffTexture.value)resources.debuffs={5,{3,4,5,6,13}};}
    else if((features&ShowDebuffs)&&debuffTexture.value){
        if(enemy)resources.debuffs=debuffs.Read(entityIndex,identity,sceneSeconds);
        else if(member<18)resources.debuffs=sceneMembers[member].row;
    }
    if(resources.debuffs.count){
        debuffTexture.Filter(resources.debuffs);
        resources.debuffs.size=static_cast<float>(debuffSize.load(std::memory_order_relaxed));
        resources.textures[3]=debuffTexture.value;
    }
    if((features&ShowLevel)&&enemy){
        resources.level=levels.Label(entityIndex,identity);
        resources.level.scale=LevelScale;
        resources.level.reserve=true;
        if(!resources.level.length){resources.level.length=5;std::memcpy(resources.level.text,"Lv.??",5);} // Placeholder until known.
    }
    if((features&(ShowTraits|ShowWeak|ShowResist))&&enemy&&traitTexture.value){
        // Use the already validated visible entity; never scan actors or retain
        // its native pointer. Only the first 24 bytes are the entity name.
        // One lookup serves traits and weaknesses/resistances.
        char name[25]{};
        if(ReadBytes(entity+offsetof(Ashita::FFXI::entity_t,Name),name,24)){
            const auto monster=LookupMonster(traitZone.load(std::memory_order_relaxed),entityIndex,name);
            const float traits=traitScale.load(std::memory_order_relaxed);
            if(features&ShowTraits){resources.traits.bits=monster.bits;resources.traits.scale=traits;}
            resources.labels.traitScale=traits;
            if(features&ShowWeak){resources.labels.weak=monster.weak;resources.labels.weakScale=weakScale.load(std::memory_order_relaxed);}
            if(features&ShowResist){resources.labels.resist=monster.resist;resources.labels.resistScale=resistScale.load(std::memory_order_relaxed);}
            resources.textures[2]=traitTexture.value;
        }
    }
    const auto number=[](TextLabel& label,unsigned value,std::uint32_t color,bool percent){
        label.color=color;
        if(value>=1000)label.text[label.length++]=static_cast<char>('0'+value/1000%10);
        if(value>=100)label.text[label.length++]=static_cast<char>('0'+value/100%10);
        if(value>=10)label.text[label.length++]=static_cast<char>('0'+value/10%10);
        label.text[label.length++]=static_cast<char>('0'+value%10);
        if(percent)label.text[label.length++]='%';
    };
    // Your own and party/alliance names deplete like monsters; the remaining
    // letters take the HP% band color, keeping the native color at 75% and above.
    if(member<18&&singleLine&&hpKnown&&friendlyHealth.load(std::memory_order_relaxed)){
        resources.healthPercent=hp;resources.healthTint=hp<75?HealthColor(hp):0;
    }
    if((features&ShowHealth)&&hpKnown)
        number(resources.labels.health,hp,HealthColor(hp),true);
    // Soft green MP and soft blue TP, half intensity under native doubled modulation.
    if(member<18&&(features&ShowMp)&&sceneMembers[member].mp>=0&&sceneMembers[member].mp<=100)
        number(resources.labels.mp,static_cast<unsigned>(sceneMembers[member].mp),0x4E734Eu,true);
    if(member<18&&(features&ShowTp)&&sceneMembers[member].tp<=3000)
        number(resources.labels.tp,sceneMembers[member].tp/10,0x466478u,true);
    const bool enlarge=selected&&member!=0&&singleLine&&growTarget.load(std::memory_order_relaxed);
    if((features&ShowDistance)||enlarge){
        float squared=0;
        if(Read(entity+offsetof(Ashita::FFXI::entity_t,Distance),squared)&&squared>=0&&squared<1e6f){
            const float distance=std::sqrt(squared);
            if(enlarge){
                // The setup-validated native routine multiplies both scales by
                // this stack argument. Use it to add only missing readability;
                // the addition fades to zero nearby, with no timer.
                float nativeFactor=0;
                if(Read(frame+0x6E0,nativeFactor))
                    resources.grow=GrowFactor(nativeFactor,distance,growFarSize.load(std::memory_order_relaxed));
            }
            const auto tenths=static_cast<unsigned>(distance*10+.5f);
            if((features&ShowDistance)&&tenths&&tenths<10000){ // Hidden at 0.0.
                auto& label=resources.labels.distance;
                number(label,tenths/10,0x808080u,false);
                label.text[label.length++]='.';
                label.text[label.length++]=static_cast<char>('0'+tenths%10);
            }
        }
    }
    if(features&ShowAction)resources.labels.action=actions.Read(entityIndex,identity,sceneSeconds);
    // Cycles neutral, success and failure colors each second.
    if(preview)resources.labels.action={10,"Thunder IV",ActionColor(sceneSeconds%3)};
    resources.labels.scale=LabelScale;
    resources.labels.actionScale=actionScale.load(std::memory_order_relaxed);
    if(member==0&&singleLine&&scrollXp.load(std::memory_order_relaxed)){xpFeed.Read(resources.labels.floating,sceneMillis);}
    // Native formatter 0x97840 already chose, ordered and stacked the icons;
    // only their placement changes, so no status flags are read.
    if(icons&&(spawnFlags&1)&&singleLine){icons->replace=true;icons->show=showIcons;}
    std::memcpy(input.expansionBase,ExpansionBase,6);std::memcpy(input.expansionCount,ExpansionCount,6);
    std::uint8_t codes[MaxGlyphs];unsigned count=0,nameCount=0;
    if(!ExpandName(input,codes,count,nameCount,icons))return false;
    std::uint32_t fontData[12]{};
    if(!ReadBytes(clientBase+native.font,fontData,sizeof(fontData))||fontData[9]!=32||fontData[10]!=142)return false;
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
    if(resources.level.length||resources.level.reserve){
        bool ready=loadGlyph(32);
        for(const char code:{'L','v','.','0','?'})ready=ready&&loadGlyph(static_cast<std::uint8_t>(code)); // Reserved width.
        for(unsigned i=0;ready&&i<resources.level.length;++i)
            ready=loadGlyph(static_cast<std::uint8_t>(resources.level.text[i]));
        if(!ready)resources.level={}; // Missing optional glyphs do not suppress the name.
        for(unsigned i=0;ready&&i<resources.level.checkLength;++i)
            if(!loadGlyph(static_cast<std::uint8_t>(resources.level.check[i])))resources.level.checkLength=0; // Level stays.
    }
    if(resources.traits.bits&&!loadGlyph(32))resources.traits={};
    if((resources.labels.weak||resources.labels.resist)&&!loadGlyph(32))resources.labels.weak=resources.labels.resist=0;
    for(auto* label:{&resources.labels.health,&resources.labels.mp,&resources.labels.tp,&resources.labels.distance,&resources.labels.action}){
        bool ready=!label->length||loadGlyph(32);
        for(unsigned i=0;ready&&i<label->length;++i)ready=loadGlyph(static_cast<std::uint8_t>(label->text[i]));
        if(!ready)*label={};
    }
    for(auto& floating:resources.labels.floating){
        auto& label=floating.text;
        bool ready=!label.length||loadGlyph(32);
        for(unsigned i=0;ready&&i<label.length;++i)ready=loadGlyph(static_cast<std::uint8_t>(label.text[i]));
        if(!ready)floating={};
    }
    std::uint32_t graphics=0,device=0;
    if(!Read(clientBase+native.graphics,graphics)||!graphics||!Read(graphics+0xC,device)||!device)return false;
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
    if(!Read(clientBase+native.config,config)||!config||!ReadBytes(config+0x10,screen,sizeof(screen)))return false;
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
    return Read(clientBase+native.graphics,graphics)&&graphics&&Read(graphics+0xC,device)&&device
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
        const auto value=filter==1?D3DTEXF_POINT:D3DTEXF_LINEAR;
        if(min==static_cast<DWORD>(value)&&mag==static_cast<DWORD>(value))return true; // Already selected: nothing to set or restore.
        changed=true;progress.nativeSafe=false;
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
    auto health=resources.healthPercent<100?MeasureHealth(output,resources.healthPercent):HealthFill{};
    health.tint=resources.healthTint;
    // The native submit changes only render target, viewport and projection, so
    // state set here holds for this whole name. Native drawing runs between names.
    unsigned group=~0u;DWORD alpha=~0u;
    // Front quads (parts the row puts on top) skip the depth test so bodies and
    // scenery never cover them; the prior ZFUNC is restored before leaving this name.
    DWORD zfunc=0;bool zSaved=false,zFront=false;
    const auto setFront=[&](bool front){
        if(front==zFront)return true;
        if(!zSaved){
            if(!progress.Check(device->GetRenderState(D3DRS_ZFUNC,&zfunc),"Get ZFUNC"))return false;
            zSaved=true;
        }
        if(!progress.Check(device->SetRenderState(D3DRS_ZFUNC,front?D3DCMP_ALWAYS:zfunc),front?"Set ZFUNC":"Restore ZFUNC"))return false;
        zFront=front;
        return true;
    };
    // Consecutive quads sharing texture, alpha reference and depth test go out as
    // one triangle list: each quad's two strip triangles, in the same order. The
    // native submit forwards type and count, drawing the list once per target.
    constexpr unsigned BatchQuads=32;
    Vertex batch[BatchQuads*6];unsigned batched=0;
    const auto flush=[&]{
        if(!batched)return true;
        if(!progress.Check(submit(resources.graphics,D3DPT_TRIANGLELIST,batched*2,batch,sizeof(Vertex)),"Submit"))return false;
        progress.submitted+=batched;batched=0;
        return true;
    };
    const auto fail=[&]{
        progress.Check(device->SetRenderState(D3DRS_ALPHAREF,0),"Restore ALPHAREF");
        if(zFront)progress.Check(device->SetRenderState(D3DRS_ZFUNC,zfunc),"Restore ZFUNC");
        return finish(false);
    };
    if(!output.count)return finish(true);
    // After state mutation or a submission, conservatively consume this
    // name. Never submit native geometry over a partial replacement.
    progress.nativeSafe=false;
    if(!progress.Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"Set ALPHABLENDENABLE")
        ||!progress.Check(device->SetVertexShader(0x144),"SetVertexShader"))return fail();
    for(unsigned i=0;i<output.count;++i){
        Quad pieces[2];
        const auto* quads=&output.quads[i];
        unsigned pieceCount=1;
        if(health.enabled&&HealthLetter(*quads)){pieceCount=HealthQuads(*quads,health,pieces);quads=pieces;}
        for(unsigned piece=0;piece<pieceCount;++piece){
            const auto& q=quads[piece];
            const bool front=(q.code&nameplate_lab::FrontCode)!=0;
            if(front!=zFront||q.textureGroup!=group||q.alphaReference!=alpha||batched==BatchQuads){
                if(!flush()||!setFront(front))return fail();
                if(q.textureGroup!=group){
                    if(!progress.Check(device->SetTexture(0,resources.textures[q.textureGroup]),"SetTexture"))return fail();
                    group=q.textureGroup;
                }
                if(q.alphaReference!=alpha){
                    if(!progress.Check(device->SetRenderState(D3DRS_ALPHAREF,q.alphaReference),"Set ALPHAREF"))return fail();
                    alpha=q.alphaReference;
                }
            }
            // Strip 0,1,2,3 draws (0,1,2) then (1,3,2), each led by the same vertex.
            auto* v=batch+batched++*6;
            v[0]=q.vertices[0];v[1]=q.vertices[1];v[2]=q.vertices[2];
            v[3]=q.vertices[1];v[4]=q.vertices[3];v[5]=q.vertices[2];
        }
    }
    if(!flush()||!setFront(false))return fail();
    if(alpha&&!progress.Check(device->SetRenderState(D3DRS_ALPHAREF,0),"Restore ALPHAREF"))return finish(false);
    return finish(true);
}
}

extern "C" {
// No dynamic allocation or plugin-object access occurs on the native draw stack.
unsigned __stdcall RenderName(std::uintptr_t frame) noexcept {
    unsigned result=0;
    if(mode.load(std::memory_order_acquire)){
        nameplate_lab::Input input{};nameplate_lab::Output output;Resources resources{};StatusIcons icons;
        DebuffBounds bounds;
        const auto visuals=Visuals();
        intentionallyFiltered=false;
        if(Collect(frame,input,resources,&icons,visuals.showStatusIcons)&&SizeName(input,visuals)&&GrowName(input,resources.grow)
            &&nameplate_lab::Build(input,output,&icons,&resources.level,&resources.traits,&resources.debuffs,&bounds,&resources.labels)){
            // Once drawing starts, do not redraw the original on top of a partial
            // replacement. A device error turns the feature off for future names.
            result=1;
            DrawProgress progress;
            const bool drawn=Draw(output,resources,nullptr,visuals.filter,&progress);
            if(drawn){
                if(bounds.occupied&&keepCursor.load(std::memory_order_relaxed))for(unsigned i=0;i<2;++i)
                    if(sceneCursorTargets[i].id==resources.identity.id&&sceneCursorTargets[i].index==resources.identity.index)
                    {
                        cursorClearance[i].IncludeBounds(resources.identity.id,bounds.top,bounds.left,bounds.right);
                    }
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
            debuffs.Clear();
            actions.Clear();
            xpFeed.Clear();
            traitZone.store(id==0x00A&&size>=0x32?PacketValue<std::uint16_t>(data,0x30):0);
            return false;
        }
        if(id==0x00E&&!blocked&&size>=0x0B){
            if((data[0x0A]&0x20)||(size>=0x20&&(data[0x0A]&4)&&data[0x1E]==0)){
                levels.Forget(PacketValue<std::uint16_t>(data,8),PacketValue<std::uint32_t>(data,4));
                debuffs.Forget(PacketValue<std::uint16_t>(data,8),PacketValue<std::uint32_t>(data,4));
                actions.Forget(PacketValue<std::uint16_t>(data,8),PacketValue<std::uint32_t>(data,4));
            }
        }
        // Original server combat data remains meaningful if a chat formatter
        // blocks it later. Observation never changes the packet/block result.
        if(id==0x028||id==0x029){
            auto* memory=core_?core_->GetMemoryManager():nullptr;
            CombatContext context{static_cast<std::uint32_t>(GetTickCount64()/1000),memory?memory->GetParty():nullptr,
                core_?core_->GetResourceManager():nullptr};
            DecodeDebuffs(id,data,size,&context,ReceiveDebuff);
            if(AnyRows(ShowAction))DecodeActions(id,data,size,&context,ReceiveAction);
        }
        // Points you gained: sender and target are both you (charutils AddExperiencePoints).
        if(id==0x02D&&!blocked&&size>=0x1A&&scrollXp.load()){
            bool chain=false;
            const auto kind=XpMessageKind(PacketValue<std::uint16_t>(data,0x18)&0x7FFF,chain);
            const auto self=LocalIdentity();
            if(kind&&self&&PacketValue<std::uint32_t>(data,4)==self&&PacketValue<std::uint32_t>(data,8)==self)
                xpFeed.Add(kind,PacketValue<std::uint32_t>(data,0x10),chain?PacketValue<std::uint32_t>(data,0x14):0,
                    static_cast<std::uint32_t>(GetTickCount64()));
        }
        // Widescan entries: target index +4, signed level +6. No identity; the
        // index is cleared on despawn and zoning.
        if(id==0x0F4&&!blocked&&size>=8)levels.Scanned(PacketValue<std::uint16_t>(data,4),static_cast<std::int8_t>(data[6]));
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
        if(id!=0x015||injected||blocked||!core_||!autoCheck.load()||!AnyRows(ShowLevel)||mode.load()<2||!LocalIdentity())return false;
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
    double GetVersion()const override{return 0.919;}
    double GetInterfaceVersion()const override{return ASHITA_INTERFACE_VERSION;}
    // Block our automatic check replies before default-priority Addons can print
    // replacement chat. Manual replies remain available to their normal handlers.
    std::int32_t GetPriority()const override{return -1;}
    std::uint32_t GetFlags()const override{return static_cast<std::uint32_t>(Ashita::PluginFlags::UseCommands|Ashita::PluginFlags::UseDirect3D|Ashita::PluginFlags::UsePackets);}
    bool Direct3DInitialize(IDirect3DDevice8* device)override{
        if(!device)return false;
        traitTextureResult=traitTexture.Initialize(device);
        debuffTextureResult=debuffTexture.Initialize(device,core_?core_->GetResourceManager():nullptr);
        if(FAILED(traitTextureResult)&&core_)
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Trait artwork unavailable (%08X); names and levels remain available.",static_cast<unsigned>(traitTextureResult));
        return true;
    }
    bool Initialize(IAshitaCore* core,ILogManager*,std::uint32_t)override{
        if(retainedModule){core->GetChatManager()->Writef(207,false,"[NameplateLab] Previous teardown could not detach safely; restart the game before loading again.");return false;}
        unsigned zero=0;
        if(!instances.compare_exchange_strong(zero,1))return false;
        owns_=true;core_=core;
        previewDebuffs=false;
        for(auto& entry:sceneMembers)entry={};
        levels.Clear();
        debuffs.Clear();
        actions.Clear();
        xpFeed.Clear();
        const auto memory=core_->GetMemoryManager();
        const auto party=memory?memory->GetParty():nullptr;
        traitZone.store(party?party->GetMemberZone(0):0);
        const auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"FFXiMain.dll"));
        if(!DiscoverNative(module)){core_->GetChatManager()->Writef(207,false,"[NameplateLab] Refused: native routine discovery failed or ambiguous.");return false;}
        ConfigureNative(module);
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
        core_->GetChatManager()->Writef(207,false,"[NameplateLab %s] Ready. /nplab opens nameplate settings; /nplab original restores native drawing.",Version);
        return true;
    }
    void Release()override{
        if(!owns_)return;
        Save(true);window_=false;
        HideTargetWindow(false); // Hand the target window back exactly as found.
        keepCursor=false;hideTarget=false;
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
        if(detached){traitTexture.Release();debuffTexture.Release();}
        debuffs.Clear();
        actions.Clear();
        xpFeed.Clear();
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
        else if(_strnicmp(option,"autocheck ",10)==0){
            const char* setting=option+10;
            if(_stricmp(setting,"on")==0||_stricmp(setting,"off")==0){options_.autoCheck=_stricmp(setting,"on")==0;ChangedVisuals();}
            else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] autocheck on | off");
        }
        else if(const auto column=DetailCommand(option)){
            // Switches one detail column in every row it applies to; the table is finer.
            const char* setting=std::strchr(option,' ')+1;
            if(_stricmp(setting,"on")==0||_stricmp(setting,"off")==0){
                for(unsigned row=0;row<RowCount;++row)
                    options_.rows[row]=_stricmp(setting,"on")==0?options_.rows[row]|(column&RowColumns[row]):options_.rows[row]&~column;
                ChangedVisuals();
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] levels | traits | debuffs | health | mp | tp | distance | actions | weakness | resistance on | off");
        }
        else if(_strnicmp(option,"xp ",3)==0){
            if(_stricmp(option+3,"on")==0||_stricmp(option+3,"off")==0){options_.scrollXp=_stricmp(option+3,"on")==0;ChangedVisuals();}
            else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] xp on | off");
        }
        else if(_strnicmp(option,"grow ",5)==0){
            if(_stricmp(option+5,"on")==0||_stricmp(option+5,"off")==0){options_.growTarget=_stricmp(option+5,"on")==0;ChangedVisuals();}
            else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] grow on | off");
        }
        else if(_strnicmp(option,"hidetarget ",11)==0){
            if(_stricmp(option+11,"on")==0||_stricmp(option+11,"off")==0){
                options_.hideTarget=_stricmp(option+11,"on")==0;
                if(options_.hideTarget&&!options_.keepCursor){options_.keepCursor=true;cursorAttempted=false;}
                ChangedVisuals();
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] hidetarget on | off");
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
                if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Name icons: %s.",options_.showStatusIcons?"native icons detached left; name centered alone":"hidden");
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
            core_->GetChatManager()->Writef(207,false,"[NameplateLab %s] %s; size %.0f%% / width %.0f%%; widescreen %s; %s filtering; recreated %u names; HP-colored %u; fallbacks %u; drawing errors %u.",Version,mode.load()==1?"Self":mode.load()==2?"All":mode.load()==3?"All + enemy HP":nameFault.load()?"Suspended (drawing error)":"Original",options_.scale*100,options_.width*100,options_.correctAspect?"corrected":"native",options_.filter==1?"sharp":options_.filter==2?"smooth":"native",replaced.load(),healthNames.load(),rejected.load(),drawingErrors.load());
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Name icons: %s.",options_.showStatusIcons?"detached left":"hidden");
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage %s; size %.0f%% / width %.2f%%; adjusted %u; rejected %u.",damageFault.load()?"unavailable (use /nplab damage retry)":damageHooked&&damageEnabled.load()?"enabled":options_.damageEnabled?"pending":"native",options_.damageScale*100,options_.damageWidth*100,damageAdjusted.load(),damageRejected.load());
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Cursor %s; forced native draws %u.",!options_.keepCursor?"off":!cursorAttempted?"pending":cursorReady&&keepCursor?"enabled":cursorProblem,cursorDraws);
            if(drawingErrors.load())core_->GetChatManager()->Writef(207,false,"[NameplateLab] Last drawing error: %s failed (HRESULT 0x%08X), %u quads submitted in that name. Select a display mode to retry.",lastDrawFailure.operation,static_cast<unsigned>(lastDrawFailure.error),lastDrawFailure.submitted);
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Intentionally filtered names: %u.",filteredNames.load());
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Private glyph submission; shared entry %s.",submissionDetoured.load()?"detoured (left unchanged)":"native");
        }else core_->GetChatManager()->Writef(207,false,"[NameplateLab] /nplab (settings) | self | all | hp | original | size <factor> | width <factor> | fit | icons show|hide | cursor on|off | hidetarget on|off | xp on|off | grow on|off | levels on|off | autocheck on|off | traits|debuffs|health|mp|tp|distance|actions|weakness|resistance on|off | damage <setting> | reset | status");
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
            changed=gui->Checkbox("Show name icons",&options_.showStatusIcons)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Player status icons the game shows by a name, with its own priority and stacking, drawn left of the name. The name and cursor center on the name alone.");
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
            if(gui->Checkbox("Hide the game's target window",&options_.hideTarget)){
                if(options_.hideTarget&&!options_.keepCursor){options_.keepCursor=true;cursorAttempted=false;}
                changed=true;
            }
            if(gui->IsItemHovered())gui->SetTooltip("Hides the target window without HideParty and restores exactly what was there when turned off or unloaded. Also keeps the native overhead cursor.");
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
            gui->SeparatorText("Scrolling XP and distant target");
            changed=gui->Checkbox("Scrolling XP from your name",&options_.scrollXp)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Experience, limit, capacity and exemplar points you gain drift down from your name and fade over 3 seconds.");
            changed=gui->Checkbox("Enlarge distant target",&options_.growTarget)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Extra enlargement fades out as you approach. Within 3 yalms the plate is exactly its ordinary size; at 20 yalms it is raised to Distant size only if needed. Your Size and Width settings still apply.");
            float growSize=options_.growFarSize*100;
            if(gui->SliderFloat("Distant size",&growSize,25,100,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.growFarSize=growSize/100;changed=true;}
            if(gui->IsItemHovered())gui->SetTooltip("Readability size at 20 yalms, relative to the game's full-size plate. Only enlarges names smaller than this; never makes nearby names bigger.");
            if(gui->Button("Reset target enlargement")){options_.growFarSize=1;changed=true;}
            gui->SeparatorText("Nameplate font");
            gui->TextUnformatted("Uses the game's loaded font, including XIPivot DATs.");
            gui->TextUnformatted("Sharp keeps pixel edges; Smooth softens scaling.");
            gui->TextUnformatted("Size and width also apply to nameplate icons.");
            gui->EndTabItem();
            }
            if(gui->BeginTabItem("Details")){
            bool preview=previewDebuffs.load();
            if(gui->Checkbox("Preview on target and yourself",&preview))previewDebuffs=preview;
            if(gui->IsItemHovered())gui->SetTooltip("Shows sample debuffs (poison, paralysis, blindness, silence, slow) and a sample action cycling white/green/red on your selected target, enemy or player, and on yourself. Not saved; turn off to see observed effects.");
            gui->SeparatorText("Plates");
            // One row per category; each name uses the first it matches, except that you keep
            // your own row when targeting yourself.
            static constexpr const char* RowNames[RowCount]={"Target","You","Party/Alliance","Claimed by you",
                "Claimed by party","Claimed by others","Unclaimed"};
            static constexpr const char* ColumnNames[]={"Show","HP%","TP","MP","Level","Traits","Debuffs","Action","Dist.","Weak","Resist"};
            static constexpr unsigned Columns[]={RowShow,ShowHealth,ShowTp,ShowMp,ShowLevel,ShowTraits,ShowDebuffs,ShowAction,ShowDistance,ShowWeak,ShowResist};
            if(gui->BeginTable("PlateRows",12,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingFixedFit)){
                gui->TableSetupColumn("");
                for(const auto* name:ColumnNames)gui->TableSetupColumn(name);
                gui->TableHeadersRow();
                for(unsigned row=0;row<RowCount;++row){
                    gui->TableNextRow();gui->TableNextColumn();gui->TextUnformatted(RowNames[row]);
                    for(unsigned c=0;c<sizeof(Columns)/sizeof(*Columns);++c){
                        gui->TableNextColumn();
                        if(!(RowColumns[row]&Columns[c])){gui->TextDisabled("-");continue;}
                        char id[16];_snprintf_s(id,sizeof(id),_TRUNCATE,"##%u.%u",row,c);
                        bool on=(options_.rows[row]&Columns[c])!=0;
                        if(gui->Checkbox(id,&on)){options_.rows[row]^=Columns[c];changed=true;}
                    }
                }
                gui->EndTable();
            }
            gui->TextUnformatted("You, then target, party/alliance, then enemies by claim.");
            gui->TextUnformatted("Other players show details only while targeted.");
            changed=gui->Checkbox("Apply features to NPCs",&options_.npcFeatures)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Off: targeted NPCs show no HP%%, distance or other details.");
            changed=gui->Checkbox("Unclaimed: only once damaged",&options_.unclaimedDamagedOnly)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Unhurt unclaimed monsters show no details unless targeted.");
            changed=gui->Checkbox("Deplete your and party/alliance names by HP",&options_.friendlyHealth)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Like monster HP: the lost part dims. Below 75%% the rest takes the HP%% color.");
            gui->SeparatorText("On top");
            if(gui->BeginTable("FrontRows",12,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingFixedFit)){
                gui->TableSetupColumn("");
                gui->TableSetupColumn("Name");
                for(unsigned c=1;c<sizeof(ColumnNames)/sizeof(*ColumnNames);++c)gui->TableSetupColumn(ColumnNames[c]);
                gui->TableHeadersRow();
                for(unsigned row=0;row<RowCount;++row){
                    gui->TableNextRow();gui->TableNextColumn();gui->TextUnformatted(RowNames[row]);
                    for(unsigned c=0;c<sizeof(Columns)/sizeof(*Columns);++c){
                        gui->TableNextColumn();
                        if(!(RowColumns[row]&Columns[c])){gui->TextDisabled("-");continue;}
                        char id[16];_snprintf_s(id,sizeof(id),_TRUNCATE,"##f%u.%u",row,c);
                        bool on=(options_.front[row]&Columns[c])!=0;
                        if(gui->Checkbox(id,&on)){options_.front[row]^=Columns[c];changed=true;}
                    }
                }
                gui->EndTable();
            }
            gui->TextUnformatted("Checked parts draw over bodies and scenery. Nearer names can still cover them.");
            gui->SeparatorText("Levels, traits, weaknesses");
            changed=gui->Checkbox("Automatically check monster targets",&options_.autoCheck)||changed;
            if(gui->IsItemHovered())gui->SetTooltip("Silent checks while any row shows levels. Manual /check still prints normally. Unknown levels show Lv.?? until a check or widescan reply.");
            gui->TextUnformatted("Traits: MobDB defaults, not current hostility. Red: aggressive, blue: passive.");
            gui->TextUnformatted("Weak/Resist: MobDB damage taken, first right of the name. Green bar: weaknesses, red bar: resistances.");
            float traitSize=options_.traitScale*100;
            if(gui->SliderFloat("Trait size",&traitSize,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.traitScale=traitSize/100;changed=true;}
            if(gui->IsItemHovered())gui->SetTooltip("Trait icons left of the level, relative to the name height.");
            if(gui->Button("Reset trait size")){options_.traitScale=.8f;changed=true;}
            float weakSize=options_.weakScale*100,resistSize=options_.resistScale*100;
            if(gui->SliderFloat("Weakness size",&weakSize,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.weakScale=weakSize/100;changed=true;}
            if(gui->IsItemHovered())gui->SetTooltip("Weapon types and elements that deal more than normal damage. Relative to the row's HP/MP/TP-column height.");
            if(gui->SliderFloat("Resistance size",&resistSize,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.resistScale=resistSize/100;changed=true;}
            if(gui->IsItemHovered())gui->SetTooltip("Weapon types and elements that deal less than normal damage, including immunity.");
            if(gui->Button("Reset weakness/resistance size")){options_.weakScale=options_.resistScale=1;changed=true;}
            if(FAILED(traitTextureResult))gui->TextUnformatted("Trait artwork unavailable; reload the plugin to try again.");
            gui->SeparatorText("Labels");
            float actionSize=options_.actionScale*100;
            if(gui->SliderFloat("Action size",&actionSize,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.actionScale=actionSize/100;changed=true;}
            if(gui->IsItemHovered())gui->SetTooltip("Readies and casts; the result stays 4 seconds: green success, red interrupted/missed/resisted.");
            if(gui->Button("Reset action size")){options_.actionScale=.6f;changed=true;}
            int iconSize=static_cast<int>(options_.debuffSize);
            if(gui->SliderInt("Debuff icon size",&iconSize,4,24,"%d",ImGuiSliderFlags_AlwaysClamp)){options_.debuffSize=static_cast<unsigned>(iconSize);changed=true;}
            if(gui->Button("Reset debuff size")){options_.debuffSize=16;changed=true;}
            if(FAILED(debuffTextureResult))gui->TextUnformatted("Debuff artwork unavailable; reload the plugin to try again.");
            gui->EndTabItem();
            }
            if(gui->BeginTabItem("Experimental")){
            gui->TextUnformatted("Nothing here right now.");
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
        const auto tick=GetTickCount64();
        sceneSeconds=static_cast<std::uint32_t>(tick/1000);sceneMillis=static_cast<std::uint32_t>(tick);
        if(requested.load())RefreshScene(core_?core_->GetMemoryManager():nullptr);
        else for(auto& entry:sceneMembers)entry={};
        cursorClearance[0]={};cursorClearance[1]={};
        if(hideTarget.load()||hiddenTarget.menu)HideTargetWindow(hideTarget.load());
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
bool DiscoverNative(std::uintptr_t base) {
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 pe{};
    if(!Read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0
        ||!Read(base+dos.e_lfanew,pe)||pe.Signature!=IMAGE_NT_SIGNATURE
        ||pe.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    const unsigned size=pe.OptionalHeader.SizeOfImage;
    if(size<4096)return false;
    // A large block can be unavailable in a 32-bit game; refuse the load cleanly.
    const std::unique_ptr<unsigned char[]> image(new(std::nothrow) unsigned char[size]());
    if(!image)return false;
    for(unsigned offset=0;offset<size;){
        const auto count=(std::min)(0x1000u,size-offset);
        ReadBytes(base+offset,image.get()+offset,count);
        offset+=count;
    }
    native={};HookRva=0;
    return native_discovery::Discover(image.get(),size,static_cast<std::uint32_t>(base),native);
}
void ConfigureNative(std::uintptr_t base) {
    clientBase=base;
    HookRva=native.nameHook;DamageRva=native.damageHook;CursorRva=native.cursorCall;SubmitRva=native.submit;
    ReadBytes(base+CursorRva,CursorOriginal,5);
    const auto nameGateAddress=reinterpret_cast<std::uintptr_t>(&NameplateGate);
    const auto damageGateAddress=reinterpret_cast<std::uintptr_t>(&DamageGate);
    const auto cursorGateAddress=reinterpret_cast<std::uintptr_t>(&CursorMenuGate);
    NameplateResume=base+native.nameResume;NameplateExit=base+native.nameExit;
    DamageResume=base+native.damageResume;
    MenuDrawAddress=base+native.menuDraw;TargetWindowAddress=base+native.targetWindow;
    CursorTailAddress=base+native.cursorTail;NameplateSubmitResume=base+SubmitRva+SubmitPrefixSize;
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
