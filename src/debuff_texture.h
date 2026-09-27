#pragma once
#include "debuffs.h"
#include <algorithm>
#include <cstring>

namespace nameplate_lab {
// SDK Bitmap starts at BITMAPINFOHEADER. The supported client supplies 32x32
// bottom-up BGRA or paletted pixels, natively with 0..128 alpha.
inline bool DecodeStatusBitmap(const std::uint8_t* bytes,unsigned length,std::uint32_t (&pixels)[1024]) noexcept {
    if(length<40)return false;
    BITMAPINFOHEADER header;std::memcpy(&header,bytes,sizeof(header));
    if(header.biSize!=40||header.biWidth!=32||header.biHeight!=32||header.biPlanes!=1
        ||header.biCompression!=BI_RGB||(header.biBitCount!=8&&header.biBitCount!=32))return false;
    const unsigned palette=header.biBitCount==8?1024:0;
    const unsigned pitch=header.biBitCount==8?32:128;
    if(length<40+palette+pitch*32)return false;
    const auto pixel=[&](unsigned x,unsigned y){
        const auto* p=bytes+40+palette+(31-y)*pitch+(header.biBitCount==8?x:x*4);
        return palette?bytes+40+*p*4:p;
    };
    // Replacement icon packs can store full 0..255 alpha; doubling those turns
    // soft glows solid. Double only artwork that stays within native 0..128.
    unsigned peak=0;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)peak=(std::max)(peak,unsigned(pixel(x,y)[3]));
    const unsigned gain=peak<=128?2:1;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){
        const auto* p=pixel(x,y);
        pixels[y*32+x]=std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)
            |((std::min)(255u,unsigned(p[3])*gain)<<24);
    }
    return true;
}
struct DebuffTexture {
    IDirect3DTexture8* value=nullptr;
    bool available[sizeof(DebuffIds)/sizeof(*DebuffIds)]{};
    void Release() noexcept {if(value){value->Release();value=nullptr;}std::memset(available,0,sizeof(available));}
    HRESULT Initialize(IDirect3DDevice8* device,IResourceManager* resources) noexcept {
        Release();
        if(!resources)return E_FAIL;
        auto result=device->CreateTexture(512,256,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&value);
        if(FAILED(result))return result;
        D3DLOCKED_RECT lock{};
        result=value->LockRect(0,&lock,nullptr,0);
        bool loaded=false;
        if(SUCCEEDED(result)){
            for(unsigned y=0;y<256;++y)std::memset(static_cast<char*>(lock.pBits)+y*lock.Pitch,0,512*4);
            for(unsigned cell=0;cell<sizeof(DebuffIds)/sizeof(*DebuffIds);++cell){
                const auto* icon=resources->GetStatusIconById(DebuffIds[cell]);
                std::uint32_t pixels[1024];
                if(!icon||!DecodeStatusBitmap(icon->Bitmap,sizeof(icon->Bitmap),pixels))continue;
                available[cell]=true;
                loaded=true;
                for(unsigned y=0;y<32;++y)
                    std::memcpy(static_cast<char*>(lock.pBits)+(cell/16*32+y)*lock.Pitch+cell%16*128,pixels+y*32,128);
            }
            result=value->UnlockRect(0);
            if(SUCCEEDED(result)&&!loaded)result=E_FAIL;
        }
        if(FAILED(result))Release();
        return result;
    }
    void Filter(DebuffRow& row) const noexcept {
        unsigned count=0;
        for(unsigned i=0;i<row.count;++i){
            const auto cell=DebuffCell(row.effects[i]);
            if(cell!=~0u&&available[cell])row.effects[count++]=row.effects[i];
        }
        row.count=count;
    }
};
}
