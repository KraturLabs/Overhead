#include "options.h"
#include <Windows.h>
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
        &&std::isfinite(v.damageScale)&&v.damageScale>=.25f&&v.damageScale<=3
        &&std::isfinite(v.damageWidth)&&v.damageWidth>=.25f&&v.damageWidth<=3;
}
bool SizeName(Input& input,const Options& v) noexcept {
    if (!ValidOptions(v)||!std::isfinite(input.scaleX)||!std::isfinite(input.scaleY)
        ||input.scaleX<=0||input.scaleY<=0||input.scaleX>128||input.scaleY>128) return false;
    // Preserve the existing internal 4:3 correction and head anchor.
    // The calculated preset also compensates for final display stretching.
    if (v.correctAspect) input.scaleX=input.scaleY*(4.0f/3.0f);
    if (v.scale!=1) { input.scaleX*=v.scale;input.scaleY*=v.scale; }
    if (v.width!=1) input.scaleX*=v.width;
    return true;
}
// The existing correction sets internal X/Y to 4:3. Account for the final
// stretch from the native name surface to the screen: width = native/screen
// aspect. The resulting on-screen X/Y is 4:3, with native vertical size.
bool OriginalWidth(float nativeAspect,unsigned screenWidth,unsigned screenHeight,float& width) noexcept {
    if(!std::isfinite(nativeAspect)||nativeAspect<=0||!screenWidth||!screenHeight
        ||screenWidth>65535||screenHeight>65535)return false;
    const auto result=static_cast<float>(static_cast<double>(nativeAspect)*screenHeight/screenWidth);
    if(!std::isfinite(result)||result<.25f||result>3)return false;
    width=result;return true;
}
bool ApplyOriginalSizing(Options& options,float nativeAspect,unsigned screenWidth,unsigned screenHeight) noexcept {
    float width=0;
    if(!OriginalWidth(nativeAspect,screenWidth,screenHeight,width))return false;
    options.scale=1;options.width=width;options.correctAspect=true;
    return true;
}
bool ApplyDamageSizing(Options& options,float nativeAspect,unsigned screenWidth,unsigned screenHeight) noexcept {
    float width=0;
    if(!OriginalWidth(nativeAspect,screenWidth,screenHeight,width))return false;
    options.damageScale=1;options.damageWidth=width;
    options.damageCorrectAspect=true;options.damageEnabled=true;return true;
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
    out.correctAspect=readChoice("CorrectAspect",1,1)!=0;
    out.filter=readChoice("Filter",2,2);
    out.mode=readChoice("Mode",3,3);
    GetPrivateProfileStringA("Nameplates","DamageScale","1",text,sizeof(text),path);ParseFactor(text,out.damageScale);
    GetPrivateProfileStringA("Nameplates","DamageWidth","1",text,sizeof(text),path);ParseFactor(text,out.damageWidth);
    out.damageEnabled=readChoice("DamageEnabled",0,1)!=0;
    out.damageCorrectAspect=readChoice("DamageCorrectAspect",0,1)!=0;
    return out;
}
bool SaveOptions(const char* path,const Options& value) noexcept {
    if(!path||!path[0]||!ValidOptions(value))return false;
    char scale[32]{},width[32]{},filter[4]{},mode[4]{};
    const auto s=std::to_chars(scale,scale+sizeof(scale)-1,value.scale);
    const auto w=std::to_chars(width,width+sizeof(width)-1,value.width);
    if(s.ec!=std::errc{}||w.ec!=std::errc{})return false;
    _snprintf_s(filter,sizeof(filter),_TRUNCATE,"%u",value.filter);
    _snprintf_s(mode,sizeof(mode),_TRUNCATE,"%u",value.mode);
    // Settings writes happen only when a control/command changes, never in the
    // native name-render callback. Windows manages the small profile file.
    bool ok=WritePrivateProfileStringA("Nameplates","Scale",scale,path)!=FALSE;
    ok=(WritePrivateProfileStringA("Nameplates","Width",width,path)!=FALSE)&&ok;
    ok=(WritePrivateProfileStringA("Nameplates","CorrectAspect",value.correctAspect?"1":"0",path)!=FALSE)&&ok;
    ok=(WritePrivateProfileStringA("Nameplates","Filter",filter,path)!=FALSE)&&ok;
    ok=(WritePrivateProfileStringA("Nameplates","Mode",mode,path)!=FALSE)&&ok;
    char damageScale[32]{},damageWidth[32]{};
    const auto ds=std::to_chars(damageScale,damageScale+sizeof(damageScale)-1,value.damageScale);
    const auto dw=std::to_chars(damageWidth,damageWidth+sizeof(damageWidth)-1,value.damageWidth);
    if(ds.ec!=std::errc{}||dw.ec!=std::errc{})return false;
    ok=(WritePrivateProfileStringA("Nameplates","DamageScale",damageScale,path)!=FALSE)&&ok;
    ok=(WritePrivateProfileStringA("Nameplates","DamageWidth",damageWidth,path)!=FALSE)&&ok;
    ok=(WritePrivateProfileStringA("Nameplates","DamageEnabled",value.damageEnabled?"1":"0",path)!=FALSE)&&ok;
    ok=(WritePrivateProfileStringA("Nameplates","DamageCorrectAspect",value.damageCorrectAspect?"1":"0",path)!=FALSE)&&ok;
    return ok;
}
}
