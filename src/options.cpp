#include "options.h"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nameplate_lab {
bool ParseFactor(const char* text, float& value) noexcept {
    if (!text) return false;
    while (*text==' ' || *text=='\t') ++text;
    const char* end=text+std::strlen(text);
    while (end>text && (end[-1]==' ' || end[-1]=='\t')) --end;
    float parsed=0;
    const auto result=std::from_chars(text,end,parsed);
    if (result.ec!=std::errc{} || result.ptr!=end || !std::isfinite(parsed) || parsed<.25f || parsed>3) return false;
    value=parsed;return true;
}
bool ValidOptions(const Options& v) noexcept {
    for(unsigned row=0;row<RowCount;++row)
        if((v.rows[row]&~RowColumns[row])||(v.front[row]&~RowColumns[row]))return false;
    return std::isfinite(v.scale)&&v.scale>=.25f&&v.scale<=3
        &&std::isfinite(v.width)&&v.width>=.25f&&v.width<=3&&v.filter<=2&&v.mode<=3
        &&v.debuffSize>=4&&v.debuffSize<=24
        &&std::isfinite(v.actionScale)&&v.actionScale>=.25f&&v.actionScale<=3
        &&std::isfinite(v.traitScale)&&v.traitScale>=.25f&&v.traitScale<=3
        &&std::isfinite(v.weakScale)&&v.weakScale>=.25f&&v.weakScale<=3
        &&std::isfinite(v.resistScale)&&v.resistScale>=.25f&&v.resistScale<=3
        &&std::isfinite(v.damageScale)&&v.damageScale>=.25f&&v.damageScale<=3
        &&std::isfinite(v.damageWidth)&&v.damageWidth>=.25f&&v.damageWidth<=3
        &&std::isfinite(v.growFarSize)&&v.growFarSize>=.25f&&v.growFarSize<=1&&v.fontOutline<=6&&v.fontSoften<=2&&v.drainRows<(1u<<RowCount)
        &&v.linkshellX>=-16&&v.linkshellX<=16&&v.linkshellY>=-16&&v.linkshellY<=16
        &&v.bazaarX>=-16&&v.bazaarX<=16&&v.bazaarY>=-16&&v.bazaarY<=16;
}
float GrowFactor(float nativeFactor,float distance,float farSize,float smoothedNative) noexcept {
    // Native factor is live stack data; zero cannot be divided out. Leave this
    // name unchanged if it is unavailable, rather than disabling the feature.
    if(!std::isfinite(nativeFactor)||nativeFactor<=0)return 1;
    if(!std::isfinite(smoothedNative)||smoothedNative<=0)smoothedNative=nativeFactor;
    // Blend from farSize at 25 yalms to the native size at 3 (user tuned).
    // The native factor flickers and rises in small steps; blending with it raw
    // pumped and following it raw shook, so the blend and its floor use the
    // smoothed factor every other name also follows. Never below that size.
    const float t=std::clamp((distance-3.f)/22.f,0.f,1.f);
    const float shown=smoothedNative+(std::max)(farSize-smoothedNative,0.f)*t*t*(3-2*t);
    return shown/nativeFactor;
}
bool GrowName(Input& input,float factor) noexcept {
    if(factor==1)return true;
    const float x=input.scaleX*factor,y=input.scaleY*factor;
    if(!std::isfinite(x)||!std::isfinite(y)||x<=0||y<=0||x>128||y>128)return false;
    input.scaleX=x;input.scaleY=y;return true;
}
bool SizeScale(float& scaleX,float& scaleY,const Appearance& v) noexcept {
    if (!std::isfinite(scaleX)||!std::isfinite(scaleY)
        ||scaleX<=0||scaleY<=0||scaleX>128||scaleY>128) return false;
    float x=scaleX,y=scaleY;
    if(v.correctAspect)x=y*(4.0f/3.0f);
    if(v.scale!=1){x*=v.scale;y*=v.scale;}
    if(v.width!=1)x*=v.width;
    if(!std::isfinite(x)||!std::isfinite(y)||x<=0||y<=0||x>128||y>128)return false;
    scaleX=x;scaleY=y;return true;
}
bool SizeName(Input& input,const Appearance& v) noexcept {
    return SizeScale(input.scaleX,input.scaleY,v);
}
// Put the complete screen correction into Width so glyph art keeps its own
// proportions (square icons stay square, as XIUI draws them); retain native height.
// Measured in game 2026-09-28: at 5120x2160 the correct factor is 0.421875.
bool OriginalWidth(unsigned screenWidth,unsigned screenHeight,float& width) noexcept {
    if(!screenWidth||!screenHeight
        ||screenWidth>65535||screenHeight>65535)return false;
    const auto result=static_cast<float>(static_cast<double>(screenHeight)/screenWidth);
    if(!std::isfinite(result)||result<.25f||result>3)return false;
    width=result;return true;
}
bool ApplyOriginalSizing(Options& options,unsigned screenWidth,unsigned screenHeight) noexcept {
    float width=0;
    if(!OriginalWidth(screenWidth,screenHeight,width))return false;
    options.scale=1;options.width=width;options.correctAspect=false;
    return true;
}
bool ApplyDamageSizing(Options& options,unsigned screenWidth,unsigned screenHeight) noexcept {
    float width=0;
    if(!OriginalWidth(screenWidth,screenHeight,width))return false;
    options.damageScale=1;options.damageWidth=width;
    options.damageCorrectAspect=false;options.damageEnabled=true;return true;
}
Options LoadOptions(const char* path) noexcept {
    Options out{};char text[64]{};
    if(!path||!path[0])return out;
    GetPrivateProfileStringA("Nameplates","Scale","1",text,sizeof(text),path);ParseFactor(text,out.scale);
    GetPrivateProfileStringA("Nameplates","Width","1",text,sizeof(text),path);ParseFactor(text,out.width);
    const auto readChoice=[&](const char* key,unsigned fallback,unsigned limit){
        GetPrivateProfileStringA("Nameplates",key,"",text,sizeof(text),path);
        unsigned parsed=0;const auto* end=text+std::strlen(text);
        const auto result=std::from_chars(text,end,parsed);
        return result.ec==std::errc{}&&result.ptr==end&&parsed<=limit?parsed:fallback;
    };
    out.correctAspect=readChoice("CorrectAspect",0,1)!=0;
    GetPrivateProfileStringA("Nameplates","FontFamily","",out.fontFamily,sizeof(out.fontFamily),path);
    out.fontOutline=readChoice("FontOutline",3,6);
    out.fontItalic=readChoice("FontItalic",0,1)!=0;
    out.fontSoften=readChoice("FontSoften",0,2);
    out.filter=readChoice("Filter",2,2);
    out.keepCursor=readChoice("KeepCursor",0,1)!=0;
    out.hideTarget=readChoice("HideTarget",0,1)!=0;
    out.showStatusIcons=readChoice("ShowStatusIcons",1,1)!=0;
    out.autoCheck=readChoice("AutoCheck",1,1)!=0;
    const auto debuffSize=readChoice("DebuffSize",16,24);
    out.debuffSize=debuffSize>=4?debuffSize:16;
    out.unclaimedDamagedOnly=readChoice("UnclaimedDamagedOnly",0,1)!=0;
    out.npcFeatures=readChoice("NpcFeatures",0,1)!=0;
    out.scrollXp=readChoice("ScrollXp",0,1)!=0;
    out.growTarget=readChoice("GrowTarget",0,1)!=0;
    {
        float farSize=1;
        GetPrivateProfileStringA("Nameplates","GrowFarSize","1",text,sizeof(text),path);
        if(ParseFactor(text,farSize))out.growFarSize=(std::min)(farSize,1.f);
    }
    static constexpr const char* RowKeys[RowCount]={"RowTarget","RowSelf","RowParty","RowClaimedSelf","RowClaimedParty","RowClaimedOther","RowUnclaimed","RowOtherPlayers"};
    if(readChoice(RowKeys[0],4096,2047)==4096){
        // Earlier per-detail toggles; their defaults produce DefaultRows.
        const auto old=[&](const char* key,unsigned fallback){return readChoice(key,fallback,1)!=0;};
        for(auto& row:out.rows)row&=RowShow|ShowTp|ShowMp;
        out.rows[RowSelf]|=ShowHealth;out.rows[RowParty]|=ShowHealth;
        const auto column=[&](unsigned bit,const char* show,const char* targetOnly,unsigned targetOnlyDefault){
            if(!old(show,1))return;
            out.rows[RowTarget]|=bit;
            if(!old(targetOnly,targetOnlyDefault))for(unsigned row=RowClaimedSelf;row<=RowUnclaimed;++row)out.rows[row]|=bit;
        };
        column(ShowLevel,"ShowLevels","LevelsTargetOnly",0);
        column(ShowTraits,"ShowTraits","TraitsTargetOnly",0);
        column(ShowDebuffs,"ShowDebuffs","DebuffsTargetOnly",0);
        column(ShowHealth,"ShowHealth","HealthTargetOnly",1);
        column(ShowDistance,"ShowDistance","DistanceTargetOnly",1);
        column(ShowAction,"ShowActions","ActionsTargetOnly",0);
        if(old("SelfDebuffs",1))out.rows[RowSelf]|=ShowDebuffs;
        if(old("PartyDebuffs",1)||old("AllianceDebuffs",1))out.rows[RowParty]|=ShowDebuffs;
        if(old("PartyActions",1)){out.rows[RowSelf]|=ShowAction;out.rows[RowParty]|=ShowAction;}
        out.rows[RowTarget]|=ShowWeak|ShowResist; // Newer than these toggles; target-only default.
    }else for(unsigned row=0;row<RowCount;++row)
        out.rows[row]=readChoice(RowKeys[row],DefaultRows[row],2047)&RowColumns[row];
    // Settings saved before weak/resist existed get its target-only default.
    GetPrivateProfileStringA("Nameplates","WeakScale","",text,sizeof(text),path);
    if(!text[0])out.rows[RowTarget]|=ShowWeak|ShowResist;
    else ParseFactor(text,out.weakScale);
    GetPrivateProfileStringA("Nameplates","ResistScale","1",text,sizeof(text),path);ParseFactor(text,out.resistScale);
    // AggroScale replaces TraitScale, whose 100% was 1.25x the weakness size.
    GetPrivateProfileStringA("Nameplates","AggroScale","",text,sizeof(text),path);
    if(text[0])ParseFactor(text,out.traitScale);
    else{
        GetPrivateProfileStringA("Nameplates","TraitScale","0.8",text,sizeof(text),path);
        float old=.8f;if(ParseFactor(text,old))out.traitScale=(std::min)(3.f,(std::max)(.25f,old/.8f));
    }
    static constexpr const char* FrontKeys[RowCount]={"FrontTarget","FrontSelf","FrontParty","FrontClaimedSelf","FrontClaimedParty","FrontClaimedOther","FrontUnclaimed","FrontOtherPlayers"};
    for(unsigned row=0;row<RowCount;++row)
        out.front[row]=readChoice(FrontKeys[row],DefaultFront[row],2047)&RowColumns[row];
    GetPrivateProfileStringA("Nameplates","ActionScale","0.6",text,sizeof(text),path);ParseFactor(text,out.actionScale);
    out.pinIcons=readChoice("PinIcons",1,1)!=0;
    out.pinOnTop=readChoice("PinOnTop",0,1)!=0;
    const auto readNudge=[&](const char* key){const int v=static_cast<int>(GetPrivateProfileIntA("Nameplates",key,0,path));return v<-16||v>16?0:v;};
    out.linkshellX=readNudge("LinkshellX");out.linkshellY=readNudge("LinkshellY");
    out.bazaarX=readNudge("BazaarX");out.bazaarY=readNudge("BazaarY");
    out.mode=readChoice("Mode",3,3);
    // Migrate the former enemy display mode and friendly checkbox only when the
    // new per-category setting has not been saved yet.
    const unsigned legacyDrain=(out.mode==3?EnemyDrainRows:0)
        |(readChoice("FriendlyHealth",0,1)?(1u<<RowSelf)|(1u<<RowParty):0);
    out.drainRows=readChoice("DrainRows",legacyDrain,(1u<<RowCount)-1);
    GetPrivateProfileStringA("Nameplates","DamageScale","1",text,sizeof(text),path);ParseFactor(text,out.damageScale);
    GetPrivateProfileStringA("Nameplates","DamageWidth","1",text,sizeof(text),path);ParseFactor(text,out.damageWidth);
    out.damageEnabled=readChoice("DamageEnabled",0,1)!=0;
    out.damageCorrectAspect=readChoice("DamageCorrectAspect",0,1)!=0;
    return out;
}
static bool WriteOptions(const char* path,const Options& value) noexcept {
    if(!path||!path[0]||!ValidOptions(value))return false;
    char scale[32]{},width[32]{},filter[4]{},mode[4]{};
    const auto s=std::to_chars(scale,scale+sizeof(scale)-1,value.scale);
    const auto w=std::to_chars(width,width+sizeof(width)-1,value.width);
    if(s.ec!=std::errc{}||w.ec!=std::errc{})return false;
    _snprintf_s(filter,sizeof(filter),_TRUNCATE,"%u",value.filter);
    _snprintf_s(mode,sizeof(mode),_TRUNCATE,"%u",value.mode);
    char damageScale[32]{},damageWidth[32]{};
    const auto ds=std::to_chars(damageScale,damageScale+sizeof(damageScale)-1,value.damageScale);
    const auto dw=std::to_chars(damageWidth,damageWidth+sizeof(damageWidth)-1,value.damageWidth);
    char actionScale[32]{},weakScale[32]{},resistScale[32]{},traitScale[32]{};
    const auto as=std::to_chars(actionScale,actionScale+sizeof(actionScale)-1,value.actionScale);
    const auto ws=std::to_chars(weakScale,weakScale+sizeof(weakScale)-1,value.weakScale);
    const auto rs=std::to_chars(resistScale,resistScale+sizeof(resistScale)-1,value.resistScale);
    const auto ts=std::to_chars(traitScale,traitScale+sizeof(traitScale)-1,value.traitScale);
    char growFarSize[32]{};
    const auto gm=std::to_chars(growFarSize,growFarSize+sizeof(growFarSize)-1,value.growFarSize);
    if(ds.ec!=std::errc{}||dw.ec!=std::errc{}||as.ec!=std::errc{}||ws.ec!=std::errc{}||rs.ec!=std::errc{}||ts.ec!=std::errc{}||gm.ec!=std::errc{})return false;
    char content[3072]{};
    const auto length=_snprintf_s(content,sizeof(content),_TRUNCATE,
        "[Nameplates]\r\nScale=%s\r\nWidth=%s\r\nCorrectAspect=%u\r\nFilter=%s\r\nMode=%s\r\nDamageScale=%s\r\nDamageWidth=%s\r\nDamageEnabled=%u\r\nDamageCorrectAspect=%u\r\nShowStatusIcons=%u\r\nKeepCursor=%u\r\nHideTarget=%u\r\nAutoCheck=%u\r\nDebuffSize=%u\r\nRowTarget=%u\r\nRowSelf=%u\r\nRowParty=%u\r\nRowClaimedSelf=%u\r\nRowClaimedParty=%u\r\nRowClaimedOther=%u\r\nRowUnclaimed=%u\r\nUnclaimedDamagedOnly=%u\r\nDrainRows=%u\r\nActionScale=%s\r\nScrollXp=%u\r\nGrowTarget=%u\r\nGrowFarSize=%s\r\nFrontTarget=%u\r\nFrontSelf=%u\r\nFrontParty=%u\r\nFrontClaimedSelf=%u\r\nFrontClaimedParty=%u\r\nFrontClaimedOther=%u\r\nFrontUnclaimed=%u\r\nNpcFeatures=%u\r\nWeakScale=%s\r\nResistScale=%s\r\nAggroScale=%s\r\nFontOutline=%u\r\nFontFamily=%s\r\nFontItalic=%u\r\nRowOtherPlayers=%u\r\nFrontOtherPlayers=%u\r\nFontSoften=%u\r\nPinIcons=%u\r\nPinOnTop=%u\r\nLinkshellX=%d\r\nLinkshellY=%d\r\nBazaarX=%d\r\nBazaarY=%d\r\n",
        scale,width,value.correctAspect?1u:0u,filter,mode,damageScale,damageWidth,
        value.damageEnabled?1u:0u,value.damageCorrectAspect?1u:0u,value.showStatusIcons?1u:0u,value.keepCursor?1u:0u,value.hideTarget?1u:0u,
        value.autoCheck?1u:0u,value.debuffSize,value.rows[0],value.rows[1],value.rows[2],value.rows[3],
        value.rows[4],value.rows[5],value.rows[6],value.unclaimedDamagedOnly?1u:0u,value.drainRows,actionScale,value.scrollXp?1u:0u,
        value.growTarget?1u:0u,growFarSize,value.front[0],value.front[1],value.front[2],value.front[3],
        value.front[4],value.front[5],value.front[6],value.npcFeatures?1u:0u,weakScale,resistScale,traitScale,value.fontOutline,value.fontFamily,value.fontItalic?1u:0u,value.rows[RowOtherPlayers],value.front[RowOtherPlayers],value.fontSoften,value.pinIcons?1u:0u,value.pinOnTop?1u:0u,value.linkshellX,value.linkshellY,value.bazaarX,value.bazaarY);
    if(length<0)return false;
    const auto file=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    bool ok=WriteFile(file,content,static_cast<DWORD>(length),&written,nullptr)!=FALSE&&written==static_cast<DWORD>(length);
    if(!CloseHandle(file))ok=false;
    return ok;
}
bool SaveOptions(const char* path,const Options& value) noexcept {
    if(!path||!path[0]||!ValidOptions(value))return false;
    char full[MAX_PATH]{},temporary[MAX_PATH]{};char* leaf=nullptr;
    const auto length=GetFullPathNameA(path,MAX_PATH,full,&leaf);
    if(!length||length>=MAX_PATH||!leaf)return false;
    char directory[MAX_PATH]{};
    const auto prefix=static_cast<std::size_t>(leaf-full);
    std::memcpy(directory,full,prefix);
    if(!GetTempFileNameA(directory,"npl",0,temporary))return false;
    // This dedicated file contains our settings. Write it completely before
    // replacing the prior file in the same directory; no forced disk flush.
    bool ok=WriteOptions(temporary,value);
    if(ok)ok=MoveFileExA(temporary,full,MOVEFILE_REPLACE_EXISTING)!=FALSE;
    if(!ok)DeleteFileA(temporary);
    return ok;
}

}
