// Resident Ashita adapter. Only this module may own native gate addresses.
#include <Ashita.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include "reload_api.h"
#include "runtime_path.h"

namespace {
struct Engine {
    HMODULE module=nullptr;
    const ReloadApi* api=nullptr;
    IPlugin* plugin=nullptr;
    wchar_t name[96]{};
};
struct Session {
    CRITICAL_SECTION lock;
    Engine current;
    IAshitaCore* core=nullptr;
    ILogManager* log=nullptr;
    IDirect3DDevice8* device=nullptr; // Borrowed from Ashita; no retained textures.
    std::uint32_t id=0;
    unsigned depth=0;
    bool owned=false,closing=false;
    std::atomic<bool> reload{false};
    Session(){InitializeCriticalSection(&lock);}
    ~Session(){DeleteCriticalSection(&lock);}
} session;
void Tell(const char* message){
    if(session.core)session.core->GetChatManager()->Writef(207,false,"[NameplateLab] %s",message);
}
void Dispose(Engine& engine){
    if(engine.plugin)engine.api->destroy(engine.plugin);
    if(engine.module)FreeLibrary(engine.module);
    engine={};
}
void CloseSession(){
    // The caller holds the resident lock and no engine callback is on the stack.
    Dispose(session.current);
    session.core=nullptr;session.log=nullptr;session.device=nullptr;
    session.owned=false;session.closing=false;session.reload.store(false);
}
struct Lock {
    bool held;
    explicit Lock(bool wait=false):held(wait? (EnterCriticalSection(&session.lock),true):TryEnterCriticalSection(&session.lock)!=FALSE){}
    ~Lock(){if(held)LeaveCriticalSection(&session.lock);}
};
struct Call {
    Call(){++session.depth;}
    ~Call(){if(--session.depth==0&&session.closing)CloseSession();}
};
bool ReadSelection(const wchar_t* root,wchar_t (&name)[96]){
    wchar_t path[MAX_PATH]{};
    if(swprintf_s(path,L"%s/current.txt",root)<0)return false;
    // Publisher atomically replaces the manifest. Read one complete generation.
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    char bytes[96]{};DWORD count=0;
    const bool read=ReadFile(file,bytes,sizeof(bytes),&count,nullptr)!=FALSE;
    CloseHandle(file);
    if(!read||count==sizeof(bytes))return false;
    while(count&&(bytes[count-1]=='\n'||bytes[count-1]=='\r'))--count;
    // No paths, traversal, arbitrary names, or loading from the DLL search path.
    if(count!=75||std::memcmp(bytes,"engine-",7)!=0||std::memcmp(bytes+71,".dll",4)!=0)return false;
    for(unsigned i=7;i<71;++i)if(!((bytes[i]>='0'&&bytes[i]<='9')||(bytes[i]>='a'&&bytes[i]<='f')))return false;
    for(unsigned i=0;i<count;++i)name[i]=static_cast<wchar_t>(bytes[i]);
    name[count]=0;return true;
}
}
extern "C" {
std::uintptr_t NameplateResume=0,NameplateExit=0,DamageResume=0;
unsigned __stdcall RenderName(std::uintptr_t frame) noexcept {
    Lock lock;
    // Contention means native rendering for this name, never a wait on a swap.
    if(!lock.held||session.closing||!session.current.plugin)return 0;
    Call call;
    return session.current.api->render(frame);
}
unsigned __stdcall RenderDamage(std::uintptr_t frame) noexcept {
    Lock lock;
    if(!lock.held||session.closing||!session.current.plugin)return 0;
    Call call;return session.current.api->damage(frame);
}
#include "native_gate.h"
}
namespace {
bool Stage(const wchar_t* root,const wchar_t* name,Engine& result){
    wchar_t path[MAX_PATH]{};
    if(swprintf_s(path,L"%s/%s",root,name)<0)return false;
    for(auto& c:path)if(c==L'/')c=L'\\';
    result.module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!result.module)return false;
    const auto query=reinterpret_cast<QueryReloadApiFn>(GetProcAddress(result.module,"NplabQueryReloadApi"));
    result.api=query?query():nullptr;
    if(!result.api||result.api->size!=sizeof(ReloadApi)||result.api->abi!=ReloadAbi
        ||result.api->sdk!=ASHITA_INTERFACE_VERSION||!result.api->create||!result.api->destroy
        ||!result.api->bind||!result.api->render||!result.api->damage){Dispose(result);return false;}
    if(!result.api->bind(reinterpret_cast<std::uintptr_t>(&NameplateGate),reinterpret_cast<std::uintptr_t>(&DamageGate))){Dispose(result);return false;}
    result.plugin=result.api->create("");
    if(!result.plugin){Dispose(result);return false;}
    wcscpy_s(result.name,name);return true;
}
bool Start(Engine& engine){
    if(!engine.plugin->Initialize(session.core,session.log,session.id))return false;
    return !session.device||engine.plugin->Direct3DInitialize(session.device);
}
bool Reload(const wchar_t* root){
    // Exclusive with every engine entry, including native callbacks. Reentrant
    // requests are deferred by BeginScene rather than destroying their caller.
    if(session.depth||session.closing)return false;
    Call transaction;
    wchar_t name[96]{};
    if(!ReadSelection(root,name)){Tell("Update selection is missing or invalid; current version retained.");return false;}
    if(session.current.module&&wcscmp(session.current.name,name)==0){Tell("Already running the published build.");return true;}
    Engine next;
    if(!Stage(root,name,next)){Tell("Update refused (DLL, interface or client check); current version retained.");return false;}
    Engine previous=session.current;
    // Release saves settings and disables/restores the old hook. Keep its DLL
    // loaded until the candidate succeeds so initialization can be rolled back.
    if(previous.plugin)previous.plugin->Release();
    session.current={};
    const bool started=next.api->bind(reinterpret_cast<std::uintptr_t>(&NameplateGate),reinterpret_cast<std::uintptr_t>(&DamageGate))&&Start(next);
    if(session.closing){Dispose(next);Dispose(previous);return false;}
    if(started){
        session.current=next;
        Dispose(previous);
        Tell("Published build loaded. /nplab status shows its version; settings were reloaded.");
        return true;
    }
    Dispose(next);
    bool recovered=false;
    if(previous.plugin&&previous.api->bind(reinterpret_cast<std::uintptr_t>(&NameplateGate),reinterpret_cast<std::uintptr_t>(&DamageGate))){
        recovered=Start(previous);
    }
    if(recovered){session.current=previous;Tell("Update initialization failed; previous build restored.");}
    else{Dispose(previous);Tell("Update initialization failed; native names remain active. Publish a working build and /nplab reload.");}
    return false;
}
class Loader final:public IPlugin {
    bool owns_=false;
public:
    const char* GetName()const override{return "NameplateLab";}
    const char* GetAuthor()const override{return "KraturLabs";}
    const char* GetDescription()const override{return "NameplateLab resident update loader";}
    double GetVersion()const override{return 0.500;}
    double GetInterfaceVersion()const override{return ASHITA_INTERFACE_VERSION;}
    std::uint32_t GetFlags()const override{return static_cast<std::uint32_t>(Ashita::PluginFlags::UseCommands|Ashita::PluginFlags::UseDirect3D);}
    bool Initialize(IAshitaCore* core,ILogManager* log,std::uint32_t id)override{
        Lock lock(true);
        if(session.owned||!core)return false;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"FFXiMain.dll"));
        if(!base)return false;
        HMODULE resident=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&NameplateGate),&resident))return false;
        NameplateResume=base+0x863E7;NameplateExit=base+0x868E7;DamageResume=base+0x41C26;
        session.core=core;session.log=log;session.id=id;session.owned=true;owns_=true;
        // Stay available in native mode if the published engine is unavailable.
        Reload(NameplateRuntimeRoot);
        return true;
    }
    void Release()override{
        Lock lock(true);
        if(!owns_)return;
        owns_=false;session.closing=true;session.reload.store(false);
        if(!session.depth)CloseSession();
    }
    bool Direct3DInitialize(IDirect3DDevice8* device)override{
        if(!device)return false;
        Lock lock(true);session.device=device;
        if(!session.current.plugin)return true;
        Call call;return session.current.plugin->Direct3DInitialize(device);
    }
    bool HandleCommand(std::int32_t type,const char* command,bool injected)override{
        if(command&&_stricmp(command,"/nplab reload")==0){session.reload.store(true);return true;}
        Lock lock;
        if(!lock.held)return command&&_strnicmp(command,"/nplab",6)==0;
        if(session.closing)return false;
        if(command&&_stricmp(command,"/nplab build")==0){
            if(session.core)session.core->GetChatManager()->Writef(207,false,"[NameplateLab] Loader 0.5.0, ABI %u; %ls",ReloadAbi,session.current.module?session.current.name:L"native fallback (no engine)");
            return true;
        }
        if(session.current.plugin){Call call;return session.current.plugin->HandleCommand(type,command,injected);}
        if(command&&_strnicmp(command,"/nplab",6)==0){Tell("Native names active; publish a working build and /nplab reload.");return true;}
        return false;
    }
    void Direct3DBeginScene(bool before)override{
        Lock lock;
        if(!lock.held||session.closing)return;
        if(!session.depth&&session.reload.exchange(false))Reload(NameplateRuntimeRoot);
        if(session.current.plugin){Call call;session.current.plugin->Direct3DBeginScene(before);}
    }
    void Direct3DPresent(const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty)override{
        Lock lock;
        if(!lock.held||session.closing||!session.current.plugin)return;
        Call call;session.current.plugin->Direct3DPresent(source,destination,window,dirty);
    }
};
}
extern "C" IPlugin* __stdcall CreatePlugin(const char*){return new(std::nothrow) Loader();}
extern "C" void __stdcall DestroyPlugin(void* instance){auto* plugin=static_cast<Loader*>(instance);if(plugin){plugin->Release();delete plugin;}}
extern "C" double __stdcall InterfaceVersion(){return ASHITA_INTERFACE_VERSION;}
