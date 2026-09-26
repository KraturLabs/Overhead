// Independent prototype. Layout/drawing is ours; assets and scene placement are
// supplied by the original game. No addon/plugin implementation is incorporated.
#include <Ashita.h>
#include <TlHelp32.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstddef>
#include <new>
#include <cmath>
#include <ffxi/entity.h>
#include <ffxi/enums.h>
#include "layout.h"
#include "health.h"
#include "options.h"
#include "client_profile.h"
#include "reload_api.h"

extern "C" {
std::uintptr_t NameplateSubmitResume=0;
#include "submission_gate.h"
}

namespace {
using namespace nameplate_lab;
constexpr std::uint32_t HookRva=0x863E1, ResumeRva=0x863E7, ExitRva=0x868E7;
constexpr std::uint32_t SubmitRva=0xCD00, SubmitPrefixSize=7;
std::atomic<bool> submissionDetoured{false};
constexpr unsigned char Original[6]={0x33,0xD2,0x33,0xED,0x33,0xF6};
std::uintptr_t clientBase=0;
std::uintptr_t residentGate=0, residentDamageGate=0;
constexpr std::uint32_t DamageRva=0x41C20;
constexpr unsigned char DamageOriginal[6]={0x81,0xEC,0x24,0x03,0x00,0x00};
unsigned char damagePatch[6]{};
bool damageHooked=false,damageHookAttempted=false;
std::atomic<bool> damageEnabled{false},damageCorrectAspect{false};
std::atomic<float> damageScale{1},damageWidth{1},nativeDamageAspect{0};
std::atomic<unsigned> damageAdjusted{0},damageRejected{0};
std::atomic<unsigned> mode{0}, requested{0}, instances{0}; // 0 original, 1 self, 2 all, 3 enemy HP.
std::atomic<unsigned> entered{0}, replaced{0}, rejected{0}, glyphCount{0}, drawingErrors{0}, active{0};
std::atomic<unsigned> healthNames{0};
std::atomic<float> nameScale{1}, nameWidth{1};
std::atomic<float> nativeNameAspect{0};
std::atomic<bool> correctAspect{true};
std::atomic<unsigned> nameFilter{2};
void PublishVisuals(const Options& options) noexcept {
    damageEnabled.store(options.damageEnabled);
    damageCorrectAspect.store(options.damageCorrectAspect);
    damageScale.store(options.damageScale);damageWidth.store(options.damageWidth);
    nameScale.store(options.scale,std::memory_order_relaxed);
    nameWidth.store(options.width,std::memory_order_relaxed);
    correctAspect.store(options.correctAspect,std::memory_order_relaxed);
    nameFilter.store(options.filter,std::memory_order_relaxed);
}
Options Visuals() noexcept {
    Options value;
    value.scale=nameScale.load(std::memory_order_relaxed);
    value.width=nameWidth.load(std::memory_order_relaxed);
    value.correctAspect=correctAspect.load(std::memory_order_relaxed);
    value.filter=nameFilter.load(std::memory_order_relaxed);
    return value;
}
unsigned char patch[6]{};
bool hooked=false;
char lastProblem[160]="not enabled";
static_assert(offsetof(Ashita::FFXI::entity_t, HPPercent) == 0xEC);
static_assert(offsetof(Ashita::FFXI::entity_t, SpawnFlags) == 0x1D0);
static_assert(offsetof(Ashita::FFXI::entity_t, ServerId) == 0x78);
constexpr auto EnemyFlag = static_cast<std::uint32_t>(Ashita::FFXI::Enums::EntitySpawnFlags::Monster);
// Player, party, alliance, ally, local player, fellow, and Trust.
constexpr std::uint32_t FriendlyFlags = 0x1 | 0x4 | 0x8 | 0x100 | 0x200 | 0x800 | 0x1000;

bool ReadBytes(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!address || size>0x10000) return false;
    __try { std::memcpy(output,reinterpret_cast<const void*>(address),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address,T& output) noexcept { return ReadBytes(address,&output,sizeof(output)); }
bool Problem(const char* value) { strcpy_s(lastProblem,value); return false; }

// Threads are paused only for the small code write, after all handles are open.
// A thread stopped inside the six-byte site makes this attempt refuse unchanged.
bool ExchangeSite(const unsigned char* expected,const unsigned char* next,std::uint32_t siteRva=HookRva) noexcept {
    HANDLE handles[512]{}; unsigned count=0, paused=0; bool ok=true;
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    if(!Thread32First(snapshot,&entry)){CloseHandle(snapshot);return false;}
    do {
        if(entry.th32OwnerProcessID!=GetCurrentProcessId() || entry.th32ThreadID==GetCurrentThreadId())continue;
        if(count==512){ok=false;break;}
        HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(!h){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;ok=false;break;}
        handles[count++]=h;
    }while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
    if(ok)for(;paused<count;++paused){
        if(SuspendThread(handles[paused])==static_cast<DWORD>(-1)){ok=false;break;}
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(handles[paused],&context)
            ||(context.Eip>=clientBase+siteRva && context.Eip<clientBase+siteRva+6)){
            ++paused;ok=false;break;
        }
    }
    auto* destination=reinterpret_cast<unsigned char*>(clientBase+siteRva);
    unsigned char current[6]{};
    DWORD protection=0,ignored=0;
    if(ok)ok=ReadBytes(reinterpret_cast<std::uintptr_t>(destination),current,6)&&std::memcmp(current,expected,6)==0;
    if(ok)ok=VirtualProtect(destination,6,PAGE_EXECUTE_READWRITE,&protection)!=FALSE;
    if(ok){
        std::memcpy(destination,next,6);
        if(!FlushInstructionCache(GetCurrentProcess(),destination,6)){
            std::memcpy(destination,expected,6);FlushInstructionCache(GetCurrentProcess(),destination,6);ok=false;
        }
        if(!VirtualProtect(destination,6,protection,&ignored)){
            std::memcpy(destination,expected,6);FlushInstructionCache(GetCurrentProcess(),destination,6);
            VirtualProtect(destination,6,protection,&ignored);ok=false;
        }
    }
    while(paused)ResumeThread(handles[--paused]);
    for(unsigned i=0;i<count;++i)CloseHandle(handles[i]);
    return ok;
}

bool Compatible() noexcept {
    if(!clientBase)return Problem("FFXiMain.dll is not loaded");
    for(const auto& range:client_profile::ranges){
        unsigned char expected[2048]{},actual[2048]{};
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

// Called through the resident lock. Validate the exact native caller and its
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
    nativeDamageAspect.store(scales[0]/scales[1]);
    if(!damageEnabled.load())return 0;
    Input input{};input.scaleX=scales[0];input.scaleY=scales[1];
    Options options;options.scale=damageScale.load();options.width=damageWidth.load();
    options.correctAspect=damageCorrectAspect.load();
    if(!SizeName(input,options)){damageRejected.fetch_add(1);return 0;}
    // The pointer is proven to be this native call's stack, not arbitrary memory.
    __try {
        auto* output=reinterpret_cast<float*>(pair);
        output[0]=input.scaleX;output[1]=input.scaleY;
    }__except(EXCEPTION_EXECUTE_HANDLER){damageRejected.fetch_add(1);return 0;}
    damageAdjusted.fetch_add(1);return 1;
}

struct Resources {
    void* graphics; IDirect3DDevice8* device; IDirect3DBaseTexture8* textures[2];
    unsigned healthPercent = 100;
};
bool Collect(std::uintptr_t frame,Input& input,Resources& resources) noexcept {
    std::uint32_t caller=0,actor=0,entity=0,identity=0,current=0;
    if(!Read(frame+0x6D4,caller)||caller!=clientBase+0xD08A8 || !Read(frame+4,actor)
        ||!Read(actor+0x70,entity)||!Read(entity+0x78,identity)||!identity)return false;
    std::uint16_t entityIndex=0;
    if(!Read(entity+0x74,entityIndex)||entityIndex>=0x900
        ||!Read(clientBase+0x480AF0+entityIndex*4,current)||current!=entity)return false;
    if(mode.load(std::memory_order_relaxed)==1){
        std::uint16_t playerIndex=0;
        if(!Read(clientBase+0x47D604,playerIndex)||!playerIndex||entityIndex!=playerIndex)return false;
    }
    if(mode.load(std::memory_order_relaxed)==3){
        std::uint32_t flags=0;std::uint8_t hp=100;
        if(Read(entity+offsetof(Ashita::FFXI::entity_t,SpawnFlags),flags)
            &&(flags&EnemyFlag)&&!(flags&FriendlyFlags)
            &&Read(entity+offsetof(Ashita::FFXI::entity_t,HPPercent),hp)&&hp<=100)
            resources.healthPercent=hp;
    }
    std::uint32_t text=0,length=0;
    if(!Read(frame+0x18,length)||!Read(frame+0x6DC,text)||length>36)return false;
    input.length=length;
    if(!ReadBytes(text,input.text,length)||!Read(frame+0x30,input.x)||!Read(frame+0x34,input.y)
        ||!Read(frame+0x48,input.z)||!Read(frame+0x4C,input.scaleX)||!Read(frame+0x50,input.scaleY)
        ||!Read(frame+0x6E4,input.nameColor)||!Read(frame+0x6E8,input.shellColor))return false;
    // An unusual multiline formatted name retains native colors for this test.
    for(unsigned i=0;i<length;++i)if(input.text[i]==10)resources.healthPercent=100;
    if(!ReadBytes(clientBase+0x32D434,input.expansionBase,6)||!ReadBytes(clientBase+0x32D43C,input.expansionCount,6))return false;
    unsigned char codes[36]{};unsigned count=0;
    const auto append=[&](unsigned char code){if(count==36)return false;codes[count++]=code;return true;};
    for(unsigned i=0;i<length;++i){
        const auto code=input.text[i];
        if(code>=0xC8&&code<=0xCD){
            const auto n=code-0xC8;if(!append(input.expansionBase[n]))return false;
            for(unsigned j=0;j<input.expansionCount[n];++j)if(!append(0xAA))return false;
        }else{if(!append(code))return false;if(code==0xAC&&!append(0xAD))return false;}
    }
    std::uint32_t fontData[12]{};
    if(!ReadBytes(clientBase+0x4E1BF8,fontData,sizeof(fontData))||fontData[9]!=32||fontData[10]!=142)return false;
    std::uint32_t table=0;
    if(!Read(fontData[11],table)||!table)return false;
    for(unsigned i=0;i<count;++i){
        const auto code=codes[i];if(code==10)continue;
        if(code<32)return false;
        auto& g=input.glyphs[code];if(g.valid)continue;
        std::uint32_t shape=0,node=0,part=0;
        if(!Read(table+(code-32)*4,shape)||!shape||!Read(shape+4,node))return false;
        bool found=false;
        for(unsigned step=0;node&&step<64;++step){
            unsigned char disabled=0;if(!Read(node+0x14,disabled))return false;
            if(!disabled){if(!Read(node+0x10,part)||!part)return false;found=true;break;}
            if(!Read(node,node))return false;
        }
        if(!found)return false;
        std::int16_t left=0,right=0,top=0,bottom=0;
        if(!Read(part+0xC0,left)||!Read(part+0xC2,right)||!Read(part+0xC8,top)||!Read(part+0xCC,bottom)
            ||!Read(shape+0x20,g.offsetX)||!Read(shape+0x24,g.offsetY))return false;
        const int width=right-left-1,height=bottom-top-1;
        if(width<0||width>256||height<0||height>256)return false;
        g.width=static_cast<std::int16_t>(width);g.height=static_cast<std::int16_t>(height);
        for(unsigned v=0;v<4;++v)if(!ReadBytes(part+0x64+v*28,g.uv+v*2,8))return false;
        g.textureGroup=code>=142?1:0;g.valid=1;
        if(!resources.textures[g.textureGroup]){
            const auto descriptor=fontData[7+g.textureGroup];std::uint32_t texture=0;
            if(!descriptor||!Read(descriptor+0x40,texture))return false;
            if(!texture&&!Read(descriptor+0x44,texture))return false;
            if(!texture)return false;
            resources.textures[g.textureGroup]=reinterpret_cast<IDirect3DBaseTexture8*>(texture);
        }
    }
    std::uint32_t graphics=0,device=0;
    if(!Read(clientBase+0x45666C,graphics)||!graphics||!Read(graphics+0xC,device)||!device)return false;
    resources.graphics=reinterpret_cast<void*>(graphics);resources.device=reinterpret_cast<IDirect3DDevice8*>(device);
    if(std::isfinite(input.scaleX)&&std::isfinite(input.scaleY)&&input.scaleX>0&&input.scaleY>0)
        nativeNameAspect.store(input.scaleX/input.scaleY,std::memory_order_relaxed);
    return true;
}

struct SizingReference {
    unsigned screenWidth=0,screenHeight=0;
    float nativeAspect=0,width=0;
};
bool ReadSizingReference(SizingReference& reference) noexcept {
    std::uint32_t config=0;std::uint16_t dimensions[6]{};
    if(!Read(clientBase+0x4568FC,config)||!config||!ReadBytes(config+0x10,dimensions,sizeof(dimensions)))return false;
    reference.screenWidth=dimensions[0];reference.screenHeight=dimensions[1];
    // Prefer the unmodified scale ratio actually received by the name renderer.
    // Before its first draw (e.g. Original mode), use the native getter fields.
    reference.nativeAspect=nativeNameAspect.load(std::memory_order_relaxed);
    if(reference.nativeAspect<=0){
        if(!dimensions[4]||!dimensions[5])return false;
        reference.nativeAspect=static_cast<float>(dimensions[4])/dimensions[5];
    }
    return OriginalWidth(reference.nativeAspect,reference.screenWidth,reference.screenHeight,reference.width);
}

// Damage uses the graphics viewport descriptor, not the name config getters.
bool ReadDamageReference(SizingReference& reference) noexcept {
    std::uint32_t config=0,graphics=0,viewports=0,width=0,height=0;
    std::uint16_t screen[2]{};unsigned char index=0;
    if(!Read(clientBase+0x4568FC,config)||!config||!ReadBytes(config+0x10,screen,sizeof(screen))
        ||!Read(clientBase+0x47BFA8,graphics)||!graphics||!Read(graphics+0xDC8,viewports)||!viewports
        ||!Read(viewports+0x158,index)||index>=8)return false;
    const auto descriptor=viewports+0x98+24*index;
    if(!Read(descriptor+8,width)||!Read(descriptor+12,height)||!width||!height||width>65535||height>65535)return false;
    reference.screenWidth=screen[0];reference.screenHeight=screen[1];
    reference.nativeAspect=static_cast<float>(width)/height;
    return OriginalWidth(reference.nativeAspect,reference.screenWidth,reference.screenHeight,reference.width);
}

using Submit=HRESULT(__thiscall*)(void*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
// Scope only our draw: never leave another UI element using our sampler choice.
struct FilterScope {
    IDirect3DDevice8* device;
    DWORD min=0,mag=0;
    bool changed=false;
    bool Apply(unsigned filter) noexcept {
        if(!filter)return true;
        if(FAILED(device->GetTextureStageState(0,D3DTSS_MINFILTER,&min))
            ||FAILED(device->GetTextureStageState(0,D3DTSS_MAGFILTER,&mag)))return false;
        changed=true;
        const auto value=filter==1?D3DTEXF_POINT:D3DTEXF_LINEAR;
        return SUCCEEDED(device->SetTextureStageState(0,D3DTSS_MINFILTER,value))
            &&SUCCEEDED(device->SetTextureStageState(0,D3DTSS_MAGFILTER,value));
    }
    bool Restore() noexcept {
        if(!changed)return true;
        changed=false;
        const auto a=device->SetTextureStageState(0,D3DTSS_MINFILTER,min);
        const auto b=device->SetTextureStageState(0,D3DTSS_MAGFILTER,mag);
        return SUCCEEDED(a)&&SUCCEEDED(b);
    }
    ~FilterScope(){Restore();}
};
bool Draw(const Output& output,const Resources& resources,unsigned* submitted=nullptr,unsigned filter=0) noexcept {
    auto* device=resources.device;
    FilterScope sampler{device};
    if(!sampler.Apply(filter))return false;
    const auto submit=reinterpret_cast<Submit>(&NameplateSubmit);
    const auto health=resources.healthPercent<100?MeasureHealth(output,resources.healthPercent):HealthFill{};
    unsigned count=0;
    for(unsigned i=0;i<output.count;++i){
        Quad pieces[2];
        const auto* quads=&output.quads[i];
        unsigned pieceCount=1;
        if(health.enabled){pieceCount=HealthQuads(*quads,health,pieces);quads=pieces;}
        for(unsigned piece=0;piece<pieceCount;++piece){
            const auto& q=quads[piece];
            // Match the original renderer's material and state sequence. Its normal
            // graphics submission helper retains the engine's scene/state handling.
            if(FAILED(device->SetTexture(0,resources.textures[q.textureGroup]))
                ||FAILED(device->SetRenderState(D3DRS_ALPHAREF,q.alphaReference))
                ||FAILED(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE))
                ||FAILED(device->SetVertexShader(0x144))
                ||FAILED(submit(resources.graphics,D3DPT_TRIANGLESTRIP,2,q.vertices,sizeof(Vertex)))){
                device->SetRenderState(D3DRS_ALPHAREF,0);return false;
            }
            device->SetRenderState(D3DRS_ALPHAREF,0);
            ++count;
        }
    }
    if(submitted)*submitted=count;
    return sampler.Restore();
}
}

extern "C" {
// No dynamic allocation or plugin-object access occurs on the native draw stack.
unsigned __stdcall RenderName(std::uintptr_t frame) noexcept {
    active.fetch_add(1,std::memory_order_relaxed);
    entered.fetch_add(1,std::memory_order_relaxed);
    unsigned result=0;
    if(mode.load(std::memory_order_acquire)){
        nameplate_lab::Input input{};nameplate_lab::Output output{};Resources resources{};
        const auto visuals=Visuals();
        if(Collect(frame,input,resources)&&SizeName(input,visuals)&&nameplate_lab::Build(input,output)){
            // Once drawing starts, do not redraw the original on top of a partial
            // replacement. A device error turns the feature off for future names.
            result=1;
            unsigned submitted=0;
            if(Draw(output,resources,&submitted,visuals.filter)){
                replaced.fetch_add(1,std::memory_order_relaxed);
                glyphCount.fetch_add(submitted,std::memory_order_relaxed);
                if(resources.healthPercent<100)healthNames.fetch_add(1,std::memory_order_relaxed);
            }else{drawingErrors.fetch_add(1,std::memory_order_relaxed);mode.store(0);requested.store(0);}
        }else rejected.fetch_add(1,std::memory_order_relaxed);
    }
    active.fetch_sub(1,std::memory_order_release);
    return result;
}

}

namespace {
bool Install() {
    if(!Compatible())return false;
    if(hooked)return true;
    if(!residentGate)return Problem("resident loader is unavailable");
    patch[0]=0xE9;patch[5]=0x90;
    const auto offset=static_cast<std::uint32_t>(residentGate-(clientBase+HookRva+5));
    std::memcpy(patch+1,&offset,4);
    if(!ExchangeSite(Original,patch))return Problem("native hook write refused; original renderer retained");
    hooked=true;return true;
}
bool InstallDamage() {
    if(!Compatible())return false;
    if(damageHooked)return true;
    if(!residentDamageGate)return Problem("resident damage gate is unavailable");
    if(!ExchangeSite(DamageOriginal,damagePatch,DamageRva))return Problem("damage hook write refused");
    damageHooked=true;return true;
}
class Plugin final:public IPlugin {
    IAshitaCore* core_=nullptr;
    bool owns_=false;
    bool window_=false,dirty_=false,saveFailed_=false,compatibilityFailed_=false;
    Options options_{};
    char settingsDirectory_[MAX_PATH]{},settingsPath_[MAX_PATH]{};
    void Save() {
        if(!dirty_)return;
        dirty_=false;
        if(!settingsPath_[0])return;
        const bool directory=CreateDirectoryA(settingsDirectory_,nullptr)!=FALSE||GetLastError()==ERROR_ALREADY_EXISTS;
        saveFailed_=!directory||!SaveOptions(settingsPath_,options_);
        if(saveFailed_&&core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Settings could not be saved; current changes last for this session.");
    }
    void SelectMode(unsigned value) {
        options_.mode=value;requested.store(value);
        if(!value)mode.store(0);
        dirty_=true;
    }
    void ChangedVisuals() { PublishVisuals(options_);dirty_=true; }
public:
    const char* GetName()const override{return "NameplateLab";}
    const char* GetAuthor()const override{return "KraturLabs";}
    const char* GetDescription()const override{return "Reloadable native nameplates with sizing, aspect correction and enemy HP color fill";}
    double GetVersion()const override{return 0.500;}
    double GetInterfaceVersion()const override{return ASHITA_INTERFACE_VERSION;}
    std::uint32_t GetFlags()const override{return static_cast<std::uint32_t>(Ashita::PluginFlags::UseCommands|Ashita::PluginFlags::UseDirect3D);}
    bool Direct3DInitialize(IDirect3DDevice8* device)override{
        // Ashita's default implementation returns false. We allocate no device
        // resources; the original game's current resources are read at draw time.
        return device!=nullptr;
    }
    bool Initialize(IAshitaCore* core,ILogManager*,std::uint32_t)override{
        unsigned zero=0;
        if(!instances.compare_exchange_strong(zero,1))return false;
        owns_=true;core_=core;
        clientBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"FFXiMain.dll"));
        if(!Compatible()){core_->GetChatManager()->Writef(207,false,"[NameplateLab] Refused: %s.",lastProblem);return false;}
        const char* root=core_->GetInstallPath();
        if(root&&_snprintf_s(settingsDirectory_,sizeof(settingsDirectory_),_TRUNCATE,"%s\\config\\nameplatelab",root)>=0
            &&_snprintf_s(settingsPath_,sizeof(settingsPath_),_TRUNCATE,"%s\\settings.ini",settingsDirectory_)>=0)
            options_=LoadOptions(settingsPath_);
        else {settingsPath_[0]=0;saveFailed_=true;}
        PublishVisuals(options_);nativeNameAspect.store(0);nativeDamageAspect.store(0);damageHookAttempted=false;
        damageAdjusted.store(0);damageRejected.store(0);
        mode.store(0);requested.store(options_.mode);
        entered.store(0);replaced.store(0);rejected.store(0);glyphCount.store(0);drawingErrors.store(0);healthNames.store(0);
        core_->GetChatManager()->Writef(207,false,"[NameplateLab 0.5.0] Ready. /nplab opens nameplate settings; /nplab original restores native drawing.");
        return true;
    }
    void Release()override{
        if(!owns_)return;
        Save();window_=false;
        mode.store(0,std::memory_order_release);requested.store(0);
        if(hooked){
            if(ExchangeSite(patch,Original))hooked=false;
            else core_->GetChatManager()->Writef(207,false,"[NameplateLab] Hook restore refused; resident gate passes through to original drawing.");
        }
        damageEnabled.store(false);
        if(damageHooked){
            if(ExchangeSite(damagePatch,DamageOriginal,DamageRva))damageHooked=false;
            else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage hook restore refused; resident gate retains native drawing.");
        }
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
        else if(_strnicmp(option,"damage ",7)==0){
            const char* setting=option+7;
            if(_stricmp(setting,"fit")==0){
                SizingReference reference;
                if(ReadDamageReference(reference)&&ApplyDamageSizing(options_,reference.nativeAspect,reference.screenWidth,reference.screenHeight))ChangedVisuals();
                else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage resolution unavailable or unsupported; settings unchanged.");
            }else if(_stricmp(setting,"off")==0){options_.damageEnabled=false;ChangedVisuals();}
            else if(_stricmp(setting,"on")==0){options_.damageEnabled=true;ChangedVisuals();}
            else if(_strnicmp(setting,"size ",5)==0||_strnicmp(setting,"width ",6)==0){
                const bool size=_strnicmp(setting,"size ",5)==0;float value=0;
                if(ParseFactor(setting+(size?5:6),value)){
                    if(size)options_.damageScale=value;else options_.damageWidth=value;
                    options_.damageEnabled=true;ChangedVisuals();
                }
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] damage fit | size <factor> | width <factor> | on | off");
        }
        else if(_stricmp(option,"fit")==0){
            SizingReference reference;
            if(ReadSizingReference(reference)&&ApplyOriginalSizing(options_,reference.nativeAspect,reference.screenWidth,reference.screenHeight)){
                ChangedVisuals();
                if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Matched original 4:3 proportions for %ux%u: size 100%%, width %.2f%%. HP/display and filtering retained.",reference.screenWidth,reference.screenHeight,options_.width*100);
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Cannot calculate 4:3 sizing from the current resolution; settings unchanged.");
        }
        else if(_stricmp(option,"reset")==0){options_=Options{};ChangedVisuals();SelectMode(options_.mode);}
        else if(_strnicmp(option,"size ",5)==0||_strnicmp(option,"width ",6)==0){
            const bool size=_strnicmp(option,"size ",5)==0;float value=0;
            if(ParseFactor(option+(size?5:6),value)){
                if(size)options_.scale=value;else options_.width=value;
                ChangedVisuals();
            }else if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Use a factor from 0.25 to 3, for example /nplab size 1.2 or /nplab width 0.85.");
        }
        else if(_stricmp(option,"status")==0){
            core_->GetChatManager()->Writef(207,false,"[NameplateLab 0.5.0] %s; size %.0f%% / width %.0f%%; widescreen %s; %s filtering; recreated %u names / %u quads; HP-colored %u; fallbacks %u; drawing errors %u.",mode.load()==1?"Self":mode.load()==2?"All":mode.load()==3?"All + enemy HP":"Original",options_.scale*100,options_.width*100,options_.correctAspect?"corrected":"native",options_.filter==1?"sharp":options_.filter==2?"smooth":"native",replaced.load(),glyphCount.load(),healthNames.load(),rejected.load(),drawingErrors.load());
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage %s; size %.0f%% / width %.2f%%; adjusted %u; rejected %u.",damageEnabled.load()?"enabled":"native",options_.damageScale*100,options_.damageWidth*100,damageAdjusted.load(),damageRejected.load());
            core_->GetChatManager()->Writef(207,false,"[NameplateLab] Private glyph submission; shared entry %s.",submissionDetoured.load()?"detoured (left unchanged)":"native");
        }else core_->GetChatManager()->Writef(207,false,"[NameplateLab] /nplab (settings) | self | all | hp | original | size <factor> | width <factor> | fit | damage <setting> | reset | status");
        Save();
        return true;
    }
    void Direct3DPresent(const RECT*,const RECT*,HWND,const RGNDATA*)override{
        if(!window_||!core_)return;
        auto* gui=core_->GetGuiManager();
        if(!gui||!gui->GetCurrentContext())return;
        gui->SetNextWindowSize(ImVec2(430,0),ImGuiCond_FirstUseEver);
        if(gui->Begin("NameplateLab",&window_,ImGuiWindowFlags_AlwaysAutoResize)){
            gui->SeparatorText("Nameplates");
            int selected=static_cast<int>(requested.load());
            if(gui->Combo("Display",&selected,"Original\0Self only\0All names\0All names + enemy HP\0"))SelectMode(static_cast<unsigned>(selected));
            bool changed=gui->Checkbox("Correct widescreen stretch",&options_.correctAspect);
            float size=options_.scale*100,width=options_.width*100;
            if(gui->SliderFloat("Size",&size,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.scale=size/100;changed=true;}
            if(gui->SliderFloat("Width",&width,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.width=width/100;changed=true;}
            int filter=static_cast<int>(options_.filter);
            if(gui->Combo("Rendering",&filter,"Native\0Sharp\0Smooth\0")){options_.filter=static_cast<unsigned>(filter);changed=true;}
            SizingReference reference;
            const bool canFit=ReadSizingReference(reference);
            gui->BeginDisabled(!canFit);
            if(gui->Button("Match original 4:3")&&canFit){
                changed=ApplyOriginalSizing(options_,reference.nativeAspect,reference.screenWidth,reference.screenHeight)||changed;
            }
            gui->EndDisabled();
            if(canFit){
                gui->Text("%ux%u baseline: Size 100%% / Width %.2f%%",reference.screenWidth,reference.screenHeight,reference.width*100);
                gui->TextUnformatted("Matches 4:3 screen proportions; keeps native height.");
            }else gui->TextUnformatted("Resolution unavailable or outside the sizing range.");
            if(gui->Button("Reset appearance")){
                options_.scale=1;options_.width=1;options_.correctAspect=true;options_.filter=2;changed=true;
            }
            gui->SeparatorText("Damage numbers");
            changed=gui->Checkbox("Adjust damage numbers",&options_.damageEnabled)||changed;
            changed=gui->Checkbox("Correct widescreen stretch##damage",&options_.damageCorrectAspect)||changed;
            float damageSize=options_.damageScale*100,damageWide=options_.damageWidth*100;
            if(gui->SliderFloat("Size##damage",&damageSize,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.damageScale=damageSize/100;options_.damageEnabled=true;changed=true;}
            if(gui->SliderFloat("Width##damage",&damageWide,25,300,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){options_.damageWidth=damageWide/100;options_.damageEnabled=true;changed=true;}
            SizingReference damageReference;
            const bool canFitDamage=ReadDamageReference(damageReference);
            gui->BeginDisabled(!canFitDamage);
            if(gui->Button("Match original 4:3##damage")&&canFitDamage)
                changed=ApplyDamageSizing(options_,damageReference.nativeAspect,damageReference.screenWidth,damageReference.screenHeight)||changed;
            gui->EndDisabled();
            if(canFitDamage)gui->Text("Baseline: Size 100%% / Width %.2f%%",damageReference.width*100);
            else gui->TextUnformatted("Damage resolution unavailable or unsupported.");
            gui->TextUnformatted("Keeps the game's damage animation, colors and font.");
            if(gui->Button("Reset damage appearance")){
                options_.damageEnabled=false;options_.damageCorrectAspect=false;
                options_.damageScale=1;options_.damageWidth=1;changed=true;
            }
            if(changed)ChangedVisuals();
            gui->SeparatorText("Nameplate font");
            gui->TextUnformatted("Uses the game's loaded font, including XIPivot DATs.");
            gui->TextUnformatted("Sharp keeps pixel edges; Smooth softens scaling.");
            gui->TextUnformatted("Size and width also apply to nameplate icons.");
            if(drawingErrors.load())gui->TextUnformatted("A drawing error disabled replacement. See /nplab status.");
            if(saveFailed_)gui->TextUnformatted("Settings could not be saved; changes are session-only.");
        }
        const bool editing=gui->IsAnyItemActive();
        gui->End();
        if(!window_||!editing)Save();
    }
    void Direct3DBeginScene(bool)override{
        if((mode.load()||damageHooked)&&!Compatible()){
            damageEnabled.store(false);
            requested.store(0);mode.store(0);
            if(!compatibilityFailed_&&core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Replacement disabled: %s.",lastProblem);
            compatibilityFailed_=true;return;
        }
        compatibilityFailed_=false;
        // Install even when disabled so we can measure native damage scaling.
        if(!damageHooked&&!damageHookAttempted){
            damageHookAttempted=true;
            if(!InstallDamage()){
                damageEnabled.store(false);
                if(core_)core_->GetChatManager()->Writef(207,false,"[NameplateLab] Damage adjustments unavailable: %s.",lastProblem);
            }
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
extern "C" void __stdcall DestroyPlugin(void* instance){auto* plugin=static_cast<Plugin*>(instance);plugin->Release();delete plugin;}
extern "C" double __stdcall InterfaceVersion(){return ASHITA_INTERFACE_VERSION;}

// Called only by the resident loader while dispatch is excluded. Recognize only
// that loader's exact gate patch, including a prior refused hook restoration.
static bool BindResidentAt(std::uintptr_t gate,std::uintptr_t base,std::uintptr_t damageGate=0) {
    if(!gate)return false;
    residentGate=gate;residentDamageGate=damageGate;
    damagePatch[0]=0xE9;damagePatch[5]=0x90;
    const auto damageOffset=static_cast<std::uint32_t>(damageGate-(base+DamageRva+5));
    std::memcpy(damagePatch+1,&damageOffset,4);
    unsigned char damageSite[6]{};
    damageHooked=base&&damageGate&&ReadBytes(base+DamageRva,damageSite,6)&&std::memcmp(damageSite,damagePatch,6)==0;
    clientBase=base;
    NameplateSubmitResume=base?base+SubmitRva+SubmitPrefixSize:0;
    patch[0]=0xE9;patch[5]=0x90;
    const auto offset=static_cast<std::uint32_t>(gate-(clientBase+HookRva+5));
    std::memcpy(patch+1,&offset,4);
    unsigned char site[6]{};
    hooked=clientBase&&ReadBytes(clientBase+HookRva,site,6)&&std::memcmp(site,patch,6)==0;
    return Compatible();
}
static bool __stdcall BindResident(std::uintptr_t gate,std::uintptr_t damageGate) {
    return BindResidentAt(gate,reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"FFXiMain.dll")),damageGate);
}
extern "C" const ReloadApi* __stdcall QueryReloadApi() {
    static const ReloadApi api{sizeof(ReloadApi),ReloadAbi,ASHITA_INTERFACE_VERSION,
        CreatePlugin,DestroyPlugin,BindResident,RenderName,AdjustDamage};
    return &api;
}
