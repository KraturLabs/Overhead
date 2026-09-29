#define NOMINMAX
#include <Windows.h>
#include "text_font.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace text_font {
namespace {
// Setup-only GDI handles, released on every exit path.
struct Preparation {
    HDC dc=CreateCompatibleDC(nullptr);
    HFONT selected=nullptr,fallback=nullptr;
    HGDIOBJ prior=dc?GetCurrentObject(dc,OBJ_FONT):nullptr;
    ~Preparation(){
        if(dc){SelectObject(dc,prior);DeleteDC(dc);}
        if(selected)DeleteObject(selected);
        if(fallback)DeleteObject(fallback);
    }
};
int CALLBACK CollectFamily(const LOGFONTW* font,const TEXTMETRICW*,DWORD type,LPARAM context) {
    if((type&RASTER_FONTTYPE)||font->lfFaceName[0]==L'@')return 1;
    char name[128]{};
    if(WideCharToMultiByte(CP_UTF8,0,font->lfFaceName,-1,name,sizeof(name),nullptr,nullptr))
        reinterpret_cast<std::vector<std::string>*>(context)->emplace_back(name);
    return 1;
}
}
std::vector<std::string> InstalledFamilies() {
    std::vector<std::string> families;
    Preparation setup;
    if(!setup.dc)return families;
    LOGFONTW filter{};filter.lfCharSet=DEFAULT_CHARSET;
    EnumFontFamiliesExW(setup.dc,&filter,CollectFamily,reinterpret_cast<LPARAM>(&families),0);
    std::sort(families.begin(),families.end());
    families.erase(std::unique(families.begin(),families.end()),families.end());
    return families;
}
bool Font::Prepare(unsigned border,const wchar_t* family,bool italic,unsigned soften) {
    error[0]=0;substitutions=0;outline=border;spill=soften?1u:0u;
    std::memset(substituted,0,sizeof(substituted));
    const auto fail=[&](const char* message){strcpy_s(error,message);return false;};
    if(border>6)return fail("Outline must be from 0 to 6.");
    if(soften>2)return fail("Edge softness must be from 0 to 2.");
    Preparation setup;
    const auto dc=setup.dc;
    if(!dc)return fail("Windows could not start drawing fonts.");
    LOGFONTW description{};description.lfWeight=FW_BOLD;wcscpy_s(description.lfFaceName,L"Tahoma");
    if(family&&*family){
        if(std::wcslen(family)>=LF_FACESIZE)return fail("That font's name is too long.");
        wcscpy_s(description.lfFaceName,family);
    }
    if(italic)description.lfItalic=TRUE;
    description.lfHeight=-48;description.lfWidth=0;
    description.lfOutPrecision=OUT_TT_ONLY_PRECIS;description.lfQuality=ANTIALIASED_QUALITY;
    setup.selected=CreateFontIndirectW(&description);
    if(!setup.selected)return fail("Windows could not load that font.");
    SelectObject(dc,setup.selected);
    if(!GetTextFaceW(dc,LF_FACESIZE,face)||_wcsicmp(face,description.lfFaceName))
        return fail("Windows offered a different font instead, so nothing was changed.");
    wchar_t characters[Characters];WORD indices[Characters];
    for(unsigned i=0;i<Characters;++i)characters[i]=static_cast<wchar_t>(First+i);
    if(GetGlyphIndicesW(dc,characters,Characters,indices,GGI_MARK_NONEXISTING_GLYPHS)==GDI_ERROR)
        return fail("Could not read which letters that font has.");
    for(unsigned i=0;i<Characters;++i)if(indices[i]==0xFFFF){substituted[i]=true;++substitutions;}
    MAT2 identity{};identity.eM11.value=1;identity.eM22.value=1;
    GLYPHMETRICS capital{};
    // Prefer H, then another available capital. Fonts lacking capitals can still
    // use their other characters with Tahoma supplying the missing text.
    const wchar_t* references=L"HABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for(const auto* c=references;*c;++c){
        if(!substituted[*c-First]&&GetGlyphOutlineW(dc,indices[*c-First],GGO_METRICS|GGO_GLYPH_INDEX,
            &capital,0,nullptr,&identity)!=GDI_ERROR&&capital.gmBlackBoxY)break;
        capital={};
    }
    setup.fallback=CreateFontW(-48,0,0,0,FW_BOLD,description.lfItalic,FALSE,FALSE,ANSI_CHARSET,
        OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Tahoma");
    if(!setup.fallback)return fail("Could not load Tahoma, the default font.");
    SelectObject(dc,setup.fallback);
    GLYPHMETRICS fallbackCapital{};
    if(GetGlyphOutlineW(dc,L'H',GGO_METRICS,&fallbackCapital,0,nullptr,&identity)==GDI_ERROR||!fallbackCapital.gmBlackBoxY)
        return fail("Could not measure Tahoma, the default font.");
    if(!capital.gmBlackBoxY)capital=fallbackCapital;
    if(substitutions&&capital.gmBlackBoxY!=fallbackCapital.gmBlackBoxY){
        const auto height=MulDiv(48,static_cast<int>(capital.gmBlackBoxY),static_cast<int>(fallbackCapital.gmBlackBoxY));
        SelectObject(dc,setup.selected);DeleteObject(setup.fallback);
        setup.fallback=CreateFontW(-std::max(1,height),0,0,0,FW_BOLD,description.lfItalic,FALSE,FALSE,ANSI_CHARSET,
            OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Tahoma");
        if(!setup.fallback)return fail("Could not size Tahoma, the default font.");
        SelectObject(dc,setup.fallback);
        if(GetGlyphOutlineW(dc,L'H',GGO_METRICS,&fallbackCapital,0,nullptr,&identity)==GDI_ERROR)
            return fail("Could not measure Tahoma, the default font.");
    }
    WORD fallbackIndices[Characters];
    if(substitutions&&GetGlyphIndicesW(dc,characters,Characters,fallbackIndices,GGI_MARK_NONEXISTING_GLYPHS)==GDI_ERROR)
        return fail("Could not read which letters Tahoma has.");
    unit=CapitalHeight/static_cast<float>(capital.gmBlackBoxY);
    baseline=capital.gmptGlyphOrigin.y*unit;
    // Measure first, then pack tallest glyphs first. Alphabetical shelves waste
    // enough vertical space to reject wide fonts with a thick outline.
    struct Raster {unsigned code,bytes;GLYPHMETRICS metrics;};
    Raster rasters[Characters]{};
    for(unsigned code=First;code<=Last;++code){
        const bool substitute=substituted[code-First];
        SelectObject(dc,substitute?setup.fallback:setup.selected);
        const auto index=substitute?fallbackIndices[code-First]:indices[code-First];
        if(index==0xFFFF)return fail("Neither this font nor Tahoma has every letter needed.");
        auto& r=rasters[code-First];r.code=code;
        r.bytes=GetGlyphOutlineW(dc,index,GGO_GRAY8_BITMAP|GGO_GLYPH_INDEX,&r.metrics,0,nullptr,&identity);
        if(r.bytes==GDI_ERROR)return fail("Windows could not draw this font's letters.");
    }
    std::sort(std::begin(rasters),std::end(rasters),[](const Raster& a,const Raster& b){
        return a.metrics.gmBlackBoxY>b.metrics.gmBlackBoxY;
    });
    // Transparent texels retain white RGB, preventing a dark fringe with linear filtering.
    pixels.assign(TextureWidth*TextureHeight,0x00FFFFFFu);
    std::memset(kerning,0,sizeof(kerning));
    constexpr unsigned gutter=2;
    const unsigned pad=Pad();
    // Separable blurs (sigma .58 and .71 raster pixel). The game samples the atlas and then
    // stretches its scene buffer; a band-limited edge steps less along slanted strokes.
    static constexpr unsigned kernels[2][3]={{1,4,1},{1,2,1}};
    const unsigned* kernel=soften?kernels[soften-1]:nullptr;
    unsigned kernelSum=0;for(unsigned i=0;kernel&&i<spill*2+1;++i)kernelSum+=kernel[i];
    const unsigned coverageScale=kernel?64u*kernelSum*kernelSum:64u;
    unsigned penX=gutter,penY=gutter,rowHeight=0;
    for(const auto& raster:rasters){
        const auto code=raster.code,bytes=raster.bytes;
        auto metrics=raster.metrics;
        auto& g=glyphs[code-First];g={};g.advance=metrics.gmCellIncX;
        if(!bytes)continue; // Spaces only advance.
        const bool substitute=substituted[code-First];
        SelectObject(dc,substitute?setup.fallback:setup.selected);
        const auto index=substitute?fallbackIndices[code-First]:indices[code-First];
        std::vector<unsigned char> bitmap(bytes);
        if(GetGlyphOutlineW(dc,index,GGO_GRAY8_BITMAP|GGO_GLYPH_INDEX,&metrics,bytes,bitmap.data(),&identity)==GDI_ERROR)
            return fail("Windows could not draw one of this font's letters.");
        const auto w=metrics.gmBlackBoxX,h=metrics.gmBlackBoxY,stride=(w+3u)&~3u;
        g.left=metrics.gmptGlyphOrigin.x-static_cast<int>(pad);g.top=-metrics.gmptGlyphOrigin.y-static_cast<int>(pad);
        if(substitute)g.top+=capital.gmptGlyphOrigin.y-fallbackCapital.gmptGlyphOrigin.y;
        g.width=static_cast<int>(w+pad*2);g.height=static_cast<int>(h+pad*2);
        if(penX+g.width+gutter>Sheet){penX=gutter;penY+=rowHeight+gutter;rowHeight=0;}
        if(g.width+2*gutter>Sheet||penY+g.height+gutter>SheetHeight)return fail("This font's letters are too big to fit. Try a lower Outline or Edge softness.");
        g.x=penX;g.y=penY;
        rowHeight=std::max(rowHeight,static_cast<unsigned>(g.height));
        penX+=g.width+gutter;
        // GDI coverage (0-64) with room for the blur's reach on every side.
        const unsigned cw=w+spill*2,ch=h+spill*2;
        std::vector<unsigned> cover(cw*ch,0u);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)cover[(y+spill)*cw+x+spill]=bitmap[y*stride+x];
        if(kernel){
            std::vector<unsigned> across(cover.size(),0u);
            const int reach=static_cast<int>(spill);
            for(unsigned y=0;y<ch;++y)for(unsigned x=0;x<cw;++x)for(int i=-reach;i<=reach;++i){
                const int sx=static_cast<int>(x)+i;
                if(sx>=0&&sx<static_cast<int>(cw))across[y*cw+x]+=kernel[i+reach]*cover[y*cw+static_cast<unsigned>(sx)];
            }
            std::fill(cover.begin(),cover.end(),0u);
            for(unsigned y=0;y<ch;++y)for(unsigned x=0;x<cw;++x)for(int i=-reach;i<=reach;++i){
                const int sy=static_cast<int>(y)+i;
                if(sy>=0&&sy<static_cast<int>(ch))cover[y*cw+x]+=kernel[i+reach]*across[static_cast<unsigned>(sy)*cw+x];
            }
        }
        for(unsigned y=0;y<ch;++y)for(unsigned x=0;x<cw;++x){
            const unsigned a=(cover[y*cw+x]*255u+coverageScale/2)/coverageScale;
            if(!a)continue; // Already transparent, and adds nothing to the outline.
            const auto px=g.x+x+border,py=g.y+y+border;
            pixels[py*TextureWidth+px]=(a<<24)|0xFFFFFFu;
            // Once per selected font: circular dilation builds the black outline's alpha mask.
            const int radius=static_cast<int>(border);
            for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
                if(dx*dx+dy*dy>radius*radius)continue;
                auto& pixel=pixels[(py+dy)*TextureWidth+px+dx+Sheet];
                pixel=(std::max(a,pixel>>24)<<24)|0xFFFFFFu;
            }
        }
    }
    // Kerning is valid only when both letters came from the same face.
    for(unsigned source=0;source<(substitutions?2u:1u);++source){
        SelectObject(dc,source?setup.fallback:setup.selected);
        const auto pairCount=GetKerningPairsW(dc,0,nullptr);
        if(!pairCount)continue;
        std::vector<KERNINGPAIR> pairs(pairCount);
        const auto read=GetKerningPairsW(dc,pairCount,pairs.data());
        for(unsigned i=0;i<read;++i){
            const auto& p=pairs[i];
            if(p.wFirst>=First&&p.wFirst<=Last&&p.wSecond>=First&&p.wSecond<=Last
                &&substituted[p.wFirst-First]==(source!=0)&&substituted[p.wSecond-First]==(source!=0))
                kerning[p.wFirst-First][p.wSecond-First]=static_cast<short>(p.iKernAmount);
        }
    }
    return true;
}

}
