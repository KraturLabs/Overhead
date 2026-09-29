#pragma once
#include <cstdint>
#include <cstring>

namespace overhead {
namespace {
#include "trait_atlas.inc"
}
// One managed texture survives ordinary D3D8 device resets. Created only during
// graphics initialization, never lazily on a name draw; released after detach.
struct TraitTexture {
    IDirect3DTexture8* value=nullptr;
    void Release() noexcept {if(value){value->Release();value=nullptr;}}
    HRESULT Initialize(IDirect3DDevice8* device) noexcept {
        Release();
        // 96 atlas rows padded to a power-of-two height; the padding stays transparent.
        auto result=device->CreateTexture(256,TraitTextureHeight,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&value);
        if(FAILED(result))return result;
        D3DLOCKED_RECT lock{};
        result=value->LockRect(0,&lock,nullptr,0);
        if(SUCCEEDED(result)){
            constexpr unsigned rows=sizeof(TraitPixels)/(256*4);
            for(unsigned y=0;y<TraitTextureHeight;++y){
                auto* line=static_cast<char*>(lock.pBits)+y*lock.Pitch;
                if(y<rows)std::memcpy(line,TraitPixels+y*256,256*4);
                else std::memset(line,0,256*4);
            }
            result=value->UnlockRect(0);
        }
        if(FAILED(result))Release();
        return result;
    }
};
}
