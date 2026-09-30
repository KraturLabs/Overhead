#pragma once
#include <cstdint>
#include <memory>
#include <new>
#include "arrow.h"

namespace overhead {
// Managed premultiplied atlas with a short mip chain; survives ordinary device
// resets. Created only during graphics initialization and released after detach.
struct ArrowTexture {
    IDirect3DTexture8* value=nullptr;
    void Release() noexcept {if(value){value->Release();value=nullptr;}}
    HRESULT Initialize(IDirect3DDevice8* device) noexcept {
        Release();
        std::unique_ptr<std::uint32_t[]> level(new(std::nothrow) std::uint32_t[ArrowAtlasWidth*ArrowAtlasHeight]);
        std::unique_ptr<std::uint32_t[]> next(new(std::nothrow) std::uint32_t[ArrowAtlasWidth*ArrowAtlasHeight/4]);
        if(!level||!next||!BuildArrowAtlas(level.get()))return E_OUTOFMEMORY;
        auto result=device->CreateTexture(ArrowAtlasWidth,ArrowAtlasHeight,ArrowAtlasLevels,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&value);
        if(FAILED(result))return result;
        unsigned width=ArrowAtlasWidth,height=ArrowAtlasHeight;
        for(unsigned mip=0;mip<ArrowAtlasLevels&&SUCCEEDED(result);++mip){
            if(mip){
                ShrinkArrowLevel(level.get(),width,height,next.get());
                width/=2;height/=2;
                std::memcpy(level.get(),next.get(),width*height*4);
            }
            D3DLOCKED_RECT lock{};
            result=value->LockRect(mip,&lock,nullptr,0);
            if(FAILED(result))break;
            for(unsigned y=0;y<height;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,level.get()+y*width,width*4);
            result=value->UnlockRect(mip);
        }
        if(FAILED(result))Release();
        return result;
    }
};
}
