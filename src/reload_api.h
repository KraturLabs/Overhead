#pragma once
#include <cstdint>
// Private, versioned contract. Both modules use the pinned official SDK and
// matching x86 compiler ABI. Objects are destroyed by their creating module.
constexpr std::uint32_t ReloadAbi=2;
struct ReloadApi {
    std::uint32_t size,abi;
    double sdk;
    IPlugin* (__stdcall *create)(const char*);
    void (__stdcall *destroy)(void*);
    bool (__stdcall *bind)(std::uintptr_t,std::uintptr_t);
    unsigned (__stdcall *render)(std::uintptr_t);
    unsigned (__stdcall *damage)(std::uintptr_t);
};
using QueryReloadApiFn=const ReloadApi* (__stdcall *)();
