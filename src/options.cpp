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
    return std::isfinite(v.scale)&&v.scale>=.25f&&v.scale<=3
        &&std::isfinite(v.width)&&v.width>=.25f&&v.width<=3&&v.filter<=2&&v.mode<=3
        &&std::isfinite(v.levelScale)&&v.levelScale>=.25f&&v.levelScale<=3
        &&std::isfinite(v.traitScale)&&v.traitScale>=.25f&&v.traitScale<=3
        &&v.debuffSize>=8&&v.debuffSize<=64
        &&!(v.rows[0]&~RowColumns[0])&&!(v.rows[1]&~RowColumns[1])&&!(v.rows[2]&~RowColumns[2])&&!(v.rows[3]&~RowColumns[3])
        &&!(v.rows[4]&~RowColumns[4])&&!(v.rows[5]&~RowColumns[5])&&!(v.rows[6]&~RowColumns[6])
        &&!(v.front[0]&~RowColumns[0])&&!(v.front[1]&~RowColumns[1])&&!(v.front[2]&~RowColumns[2])&&!(v.front[3]&~RowColumns[3])
        &&!(v.front[4]&~RowColumns[4])&&!(v.front[5]&~RowColumns[5])&&!(v.front[6]&~RowColumns[6])
        &&std::isfinite(v.labelScale)&&v.labelScale>=.25f&&v.labelScale<=3
        &&std::isfinite(v.actionScale)&&v.actionScale>=.25f&&v.actionScale<=3
        &&std::isfinite(v.damageScale)&&v.damageScale>=.25f&&v.damageScale<=3
        &&std::isfinite(v.damageWidth)&&v.damageWidth>=.25f&&v.damageWidth<=3
        &&std::isfinite(v.growFarSize)&&v.growFarSize>=.25f&&v.growFarSize<=1;
}
float GrowFactor(float nativeFactor,float distance,float farSize) noexcept {
    // Native factor is live stack data; zero cannot be divided out. Leave this
    // name unchanged if it is unavailable, rather than disabling the feature.
    if(!std::isfinite(nativeFactor)||nativeFactor<=0)return 1;
    const float t=std::clamp((distance-3.f)/17.f,0.f,1.f);
    const float extra=(std::max)(farSize/nativeFactor-1,0.f);
    return 1+extra*t*t*(3-2*t);
}
bool GrowName(Input& input,float factor) noexcept {
    if(factor==1)return true;
    const float x=input.scaleX*factor,y=input.scaleY*factor;
    if(!std::isfinite(x)||!std::isfinite(y)||x<=0||y<=0||x>128||y>128)return false;
    input.scaleX=x;input.scaleY=y;return true;
}
bool SizeScale(float& scaleX,float& scaleY,const Options& v) noexcept {
    if (!std::isfinite(scaleX)||!std::isfinite(scaleY)
        ||scaleX<=0||scaleY<=0||scaleX>128||scaleY>128) return false;
    float x=scaleX,y=scaleY;
    if(v.correctAspect)x=y*(4.0f/3.0f);
    if(v.scale!=1){x*=v.scale;y*=v.scale;}
    if(v.width!=1)x*=v.width;
    if(!std::isfinite(x)||!std::isfinite(y)||x<=0||y<=0||x>128||y>128)return false;
    scaleX=x;scaleY=y;return true;
}
bool SizeName(Input& input,const Options& v) noexcept {
    return SizeScale(input.scaleX,input.scaleY,v);
}
// Put the complete 4:3 screen correction into Width; retain native height.
bool OriginalWidth(unsigned screenWidth,unsigned screenHeight,float& width) noexcept {
    if(!screenWidth||!screenHeight
        ||screenWidth>65535||screenHeight>65535)return false;
    const auto result=static_cast<float>((4.0/3.0)*screenHeight/screenWidth);
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
    out.filter=readChoice("Filter",2,2);
    out.keepCursor=readChoice("KeepCursor",0,1)!=0;
    out.hideTarget=readChoice("HideTarget",0,1)!=0;
    out.showStatusIcons=readChoice("ShowStatusIcons",1,1)!=0;
    out.autoCheck=readChoice("AutoCheck",1,1)!=0;
    GetPrivateProfileStringA("Nameplates","LevelScale","1",text,sizeof(text),path);ParseFactor(text,out.levelScale);
    const auto debuffSize=readChoice("DebuffSize",16,64);
    out.debuffSize=debuffSize>=8?debuffSize:16;
    out.unclaimedDamagedOnly=readChoice("UnclaimedDamagedOnly",0,1)!=0;
    out.friendlyHealth=readChoice("FriendlyHealth",0,1)!=0;
    out.scrollXp=readChoice("ScrollXp",0,1)!=0;
    out.xpUp=readChoice("XpUp",0,1)!=0;
    out.growTarget=readChoice("GrowTarget",0,1)!=0;
    {
        float farSize=1;
        GetPrivateProfileStringA("Nameplates","GrowFarSize","1",text,sizeof(text),path);
        if(ParseFactor(text,farSize))out.growFarSize=(std::min)(farSize,1.f);
    }
    static constexpr const char* RowKeys[RowCount]={"RowTarget","RowSelf","RowParty","RowClaimedSelf","RowClaimedParty","RowClaimedOther","RowUnclaimed"};
    if(readChoice(RowKeys[0],1024,1023)==1024){
        // Earlier per-detail toggles; their defaults produce DefaultRows.
        const auto old=[&](const char* key,unsigned fallback){return readChoice(key,fallback,1)!=0;};
        for(auto& row:out.rows)row&=RowShow|ShowTp|ShowMp;
        out.rows[RowSelf]|=ShowHealth;out.rows[RowParty]|=ShowHealth;
        const auto column=[&](unsigned bit,const char* show,const char* targetOnly,unsigned targetOnlyDefault){
            if(!old(show,1))return;
            out.rows[RowTarget]|=bit;
            if(!old(targetOnly,targetOnlyDefault))for(unsigned row=RowClaimedSelf;row<RowCount;++row)out.rows[row]|=bit;
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
    }else for(unsigned row=0;row<RowCount;++row)
        out.rows[row]=readChoice(RowKeys[row],DefaultRows[row],1023)&RowColumns[row];
    static constexpr const char* FrontKeys[RowCount]={"FrontTarget","FrontSelf","FrontParty","FrontClaimedSelf","FrontClaimedParty","FrontClaimedOther","FrontUnclaimed"};
    for(unsigned row=0;row<RowCount;++row)
        out.front[row]=readChoice(FrontKeys[row],DefaultFront[row],1023)&RowColumns[row];
    GetPrivateProfileStringA("Nameplates","LabelScale","1",text,sizeof(text),path);ParseFactor(text,out.labelScale);
    GetPrivateProfileStringA("Nameplates","ActionScale","0.6",text,sizeof(text),path);ParseFactor(text,out.actionScale);
    GetPrivateProfileStringA("Nameplates","TraitScale","1",text,sizeof(text),path);ParseFactor(text,out.traitScale);
    out.mode=readChoice("Mode",3,3);
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
    char damageScale[32]{},damageWidth[32]{},levelScale[32]{},traitScale[32]{};
    const auto ts=std::to_chars(traitScale,traitScale+sizeof(traitScale)-1,value.traitScale);
    const auto ls=std::to_chars(levelScale,levelScale+sizeof(levelScale)-1,value.levelScale);
    const auto ds=std::to_chars(damageScale,damageScale+sizeof(damageScale)-1,value.damageScale);
    const auto dw=std::to_chars(damageWidth,damageWidth+sizeof(damageWidth)-1,value.damageWidth);
    char labelScale[32]{},actionScale[32]{};
    const auto xs=std::to_chars(labelScale,labelScale+sizeof(labelScale)-1,value.labelScale);
    const auto as=std::to_chars(actionScale,actionScale+sizeof(actionScale)-1,value.actionScale);
    char growFarSize[32]{};
    const auto gm=std::to_chars(growFarSize,growFarSize+sizeof(growFarSize)-1,value.growFarSize);
    if(ds.ec!=std::errc{}||dw.ec!=std::errc{}||ls.ec!=std::errc{}||ts.ec!=std::errc{}||xs.ec!=std::errc{}||as.ec!=std::errc{}||gm.ec!=std::errc{})return false;
    char content[1536]{};
    const auto length=_snprintf_s(content,sizeof(content),_TRUNCATE,
        "[Nameplates]\r\nScale=%s\r\nWidth=%s\r\nCorrectAspect=%u\r\nFilter=%s\r\nMode=%s\r\nDamageScale=%s\r\nDamageWidth=%s\r\nDamageEnabled=%u\r\nDamageCorrectAspect=%u\r\nShowStatusIcons=%u\r\nKeepCursor=%u\r\nHideTarget=%u\r\nAutoCheck=%u\r\nLevelScale=%s\r\nTraitScale=%s\r\nDebuffSize=%u\r\nRowTarget=%u\r\nRowSelf=%u\r\nRowParty=%u\r\nRowClaimedSelf=%u\r\nRowClaimedParty=%u\r\nRowClaimedOther=%u\r\nRowUnclaimed=%u\r\nUnclaimedDamagedOnly=%u\r\nFriendlyHealth=%u\r\nLabelScale=%s\r\nActionScale=%s\r\nScrollXp=%u\r\nXpUp=%u\r\nGrowTarget=%u\r\nGrowFarSize=%s\r\nFrontTarget=%u\r\nFrontSelf=%u\r\nFrontParty=%u\r\nFrontClaimedSelf=%u\r\nFrontClaimedParty=%u\r\nFrontClaimedOther=%u\r\nFrontUnclaimed=%u\r\n",
        scale,width,value.correctAspect?1u:0u,filter,mode,damageScale,damageWidth,
        value.damageEnabled?1u:0u,value.damageCorrectAspect?1u:0u,value.showStatusIcons?1u:0u,value.keepCursor?1u:0u,value.hideTarget?1u:0u,
        value.autoCheck?1u:0u,levelScale,traitScale,value.debuffSize,value.rows[0],value.rows[1],value.rows[2],value.rows[3],
        value.rows[4],value.rows[5],value.rows[6],value.unclaimedDamagedOnly?1u:0u,value.friendlyHealth?1u:0u,labelScale,actionScale,value.scrollXp?1u:0u,value.xpUp?1u:0u,
        value.growTarget?1u:0u,growFarSize,value.front[0],value.front[1],value.front[2],value.front[3],
        value.front[4],value.front[5],value.front[6]);
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
