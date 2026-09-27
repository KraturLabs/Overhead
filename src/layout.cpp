#include "layout.h"
#include "debuffs.h"
#include "traits.h"
#include <algorithm>
#include <cmath>

namespace nameplate_lab {
namespace {
bool finite(float value) noexcept { return std::isfinite(value); }
float rounded(double value) noexcept { return static_cast<float>(value); }
bool validGlyph(const Glyph& g) noexcept {
    if(!g.valid||g.width<0||g.width>256||g.height<0||g.height>256||g.textureGroup>1
        ||g.offsetX<-256||g.offsetX>256||g.offsetY<-256||g.offsetY>256)return false;
    for(float uv:g.uv)if(!finite(uv))return false;
    return true;
}
}

bool ExpandName(const Input& in, std::uint8_t (&codes)[MaxGlyphs], unsigned& count,
    unsigned& nameCount, const StatusIcons* icons) noexcept {
    count = 0;
    if (in.length > MaxGlyphs) return false;
    const auto append = [&](std::uint8_t code) {
        if (count == MaxGlyphs) return false;
        codes[count++] = code;
        return true;
    };
    const auto expand = [&](std::uint8_t code) {
        if (code >= 0xC8 && code <= 0xCD) {
            const unsigned n = code - 0xC8;
            if (!append(in.expansionBase[n])) return false;
            for (unsigned j = 0; j < in.expansionCount[n]; ++j)
                if (!append(0xAA)) return false;
            return true;
        }
        return append(code) && (code != 0xAC || append(0xAD));
    };
    // The native prefix is a run of icon codes, then one space when its main
    // slot is occupied. Icon-only labels keep native placement.
    unsigned prefix = 0, begin = 0;
    if (icons && icons->replace) {
        while (prefix < in.length && in.text[prefix] >= 0x8E) ++prefix;
        begin = prefix;
        if (prefix && begin < in.length && in.text[begin] == 32) ++begin;
        if (begin == in.length) prefix = begin = 0;
    }
    for (unsigned i = begin; i < in.length; ++i)
        if (!expand(in.text[i])) return false;
    nameCount = count;
    if (icons && icons->show)
        for (unsigned i = 0; i < prefix; ++i)
            if (!expand(in.text[i])) return false;
    return true;
}

bool Build(const Input& in, Output& out, const StatusIcons* icons, const LevelLabel* level, const TraitLabel* traits,
    const DebuffRow* debuffs,DebuffBounds* bounds,const SideLabels* labels) noexcept {
    if(bounds)*bounds={};
    out.count = 0;
    if (in.length > MaxGlyphs || !finite(in.x) || !finite(in.y) || !finite(in.z)
        || !finite(in.scaleX) || !finite(in.scaleY) || in.scaleX <= 0 || in.scaleY <= 0
        || in.scaleX > 128 || in.scaleY > 128 || in.z < 0 || in.z >= 1) return false;
    std::uint8_t codes[MaxGlyphs];
    unsigned count = 0, nameCount = 0;
    if (!ExpandName(in, codes, count, nameCount, icons)) return false;
    struct Size { int width, height; } sizes[MaxGlyphs]{};
    // Name and detached prefix each follow the native rules from their own first
    // glyph: later icons shrink and overlap the one before them. The name is
    // centered on its native width; the prefix ends where the native pen would.
    int total = 0;
    float iconAdvance = 0;
    const Glyph* textReference=nullptr;
    for (unsigned i = 0; i < count; ++i) {
        const auto code = codes[i];
        if (code == 10) continue;
        if (code < 32) return false;
        const auto& g = in.glyphs[code];
        if (!validGlyph(g)) return false;
        if(i<nameCount&&!textReference&&code>32&&code<142)textReference=&g;
        const bool detached = i >= nameCount;
        const unsigned index = detached ? i - nameCount : i;
        auto& size = sizes[i];
        size = {g.width, g.height};
        int width = 0;
        double advance = size.width;
        if (index && code >= 0x8E && code <= 0xA8) {
            width = size.width / 2;
            size.width = static_cast<int>(static_cast<double>(size.width) * 0.8f);
            size.height = static_cast<int>(static_cast<double>(size.height) * 0.8f);
            advance = size.width * 0.625;
        } else if (index && code == 0xAA) {
            size.width /= 2;
            size.height /= 2;
            width = size.width / 2;
            advance = size.width * 0.5;
        } else if (code != 0xAD) width = size.width;
        else advance = 0;
        if (detached) iconAdvance = rounded(iconAdvance + advance);
        else total += width;
    }
    const float start = rounded(-total * 0.5);
    float pen = start, line = 0;
    const unsigned iconCount = count - nameCount;
    if (iconCount && !in.glyphs[32].valid) return false;
    // The prefix ends one loaded-font space before the name, as natively.
    const float gap = iconCount ? static_cast<float>(in.glyphs[32].width) : 0;
    const float iconStart = start - iconAdvance - gap;
    float detailLeft=iconStart;
    for (unsigned i = 0; i < count; ++i) {
        const auto code = codes[i];
        if (code == 10) { pen = start; line += 8; continue; }
        const bool detached = i >= nameCount;
        if (detached && i == nameCount) { pen = iconStart; line = 0; }
        const unsigned index = detached ? i - nameCount : i;
        const auto& g = in.glyphs[code];
        const auto size = sizes[i];
        double scale = 1, advance = size.width;
        int dx = 0, dy = 0;
        if (index && code >= 0x8E && code <= 0xA8) {
            scale = 0.8f; advance = size.width * 0.625;
            dx = -(size.width / 2); dy = size.height / 2;
        } else if (index && code == 0xAA) {
            advance = size.width * 0.5; dx = -size.width; dy = size.height;
        } else if (code == 0xAD) {
            scale = 0.5; advance = 0; dx = -2-size.width; dy = -2;
        }
        const double leftExact = static_cast<double>(g.offsetX + dx) + pen;
        const float left = rounded(leftExact);
        detailLeft=std::min(detailLeft,left);
        const float right = rounded(leftExact + size.width * scale);
        const float top = rounded(static_cast<double>(g.offsetY + dy) + line);
        const float bottom = rounded(static_cast<double>(rounded(size.height * scale)) + top);
        pen = rounded(advance + pen);
        auto& q = out.quads[out.count++];
        q.code = code; q.textureGroup = g.textureGroup;
        // The native formatter already set the shell color: linkshell tint only
        // when its main icon is the linkshell, otherwise neutral.
        auto color = g.textureGroup == 1 ? in.shellColor : in.nameColor;
        q.alphaReference = detached || color == in.shellColor ? 0x60u : 0u;
        if (q.alphaReference) {
            const auto rgb = code == 0x92 ? color : 0x80808080u;
            color = 0x80000000u | ((rgb & 255u) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 255u);
        }
        for (unsigned v = 0; v < 4; ++v) {
            const float localX = v & 1 ? right : left;
            const float localY = v & 2 ? bottom : top;
            q.vertices[v] = {
                rounded(static_cast<double>(in.x) + rounded(static_cast<double>(in.scaleX) * localX)),
                rounded(static_cast<double>(in.y) + rounded(static_cast<double>(in.scaleY) * localY)),
                in.z, 1, color, g.uv[v*2], g.uv[v*2+1]
            };
        }
    }
    // Native-font runs: decorations (0x100|code) stay out of HP bounds/coloring.
    // Scale grows from the run's own baseline; shift moves it down in local units.
    // '%' is drawn smaller than the digits it follows, on the same baseline.
    constexpr float Symbol=.7f;
    const auto measure=[&](const char* text,unsigned length,float& width){
        width=0;
        for(unsigned i=0;i<length;++i){
            const auto& g=in.glyphs[static_cast<unsigned char>(text[i])];
            if(!validGlyph(g))return false; // Optional label unavailable; keep the name.
            width+=g.width*(text[i]=='%'?Symbol:1);
        }
        return length>0;
    };
    const auto write=[&](const char* text,unsigned length,std::uint32_t color,float scale,float pen,float shift,float alpha=1){
        const auto opacity=static_cast<std::uint32_t>(static_cast<float>(in.nameColor>>24)*alpha)<<24;
        const auto& reference=in.glyphs[static_cast<unsigned char>(text[0])];
        const float baseline=static_cast<float>(reference.offsetY+reference.height);
        float leftmost=pen;
        for(unsigned i=0;i<length;++i){
            const auto code=static_cast<unsigned char>(text[i]);
            const auto& g=in.glyphs[code];
            const float size=code=='%'?scale*Symbol:scale;
            leftmost=std::min(leftmost,pen+size*g.offsetX);
            auto& q=out.quads[out.count++];
            q.code=0x100u|code;
            q.textureGroup=g.textureGroup;q.alphaReference=0;
            for(unsigned v=0;v<4;++v){
                q.vertices[v]={
                    in.x+in.scaleX*(pen+size*(g.offsetX+(v&1?g.width:0))),
                    in.y+in.scaleY*(shift+baseline+size*(g.offsetY+(v&2?g.height:0)-baseline)),in.z,1,
                    opacity|color,g.uv[v*2],g.uv[v*2+1]};
            }
            pen+=g.width*size;
        }
        return leftmost;
    };
    if(level&&level->length&&level->length<=6&&nameCount&&validGlyph(in.glyphs[32])){
        float width=0;
        if(!measure(level->text,level->length,width))return true;
        const float labelPen=iconStart-width*level->scale-in.glyphs[32].width;
        detailLeft=std::min(detailLeft,labelPen);
        detailLeft=std::min(detailLeft,write(level->text,level->length,level->color,level->scale,labelPen,0));
    }
    if(labels&&nameCount&&validGlyph(in.glyphs[32])){
        // HP%, MP, TP then distance follow the name's advance on its baseline; the action
        // overlaps its lower right corner. None shift the name, level, traits or cursor.
        const float space=static_cast<float>(in.glyphs[32].width),end=start+total;
        float next=end+space,width=0;
        const auto single=[&](const TextLabel& label,unsigned limit){
            if(label.length>limit||!measure(label.text,label.length,width))return;
            write(label.text,label.length,label.color,labels->scale,next,0);next+=width*labels->scale+space;
        };
        single(labels->health,4);
        // MP stacks over TP within the name's height with a small gap, nudged
        // slightly down; either alone uses a fixed 65% size.
        float mpWidth=0,tpWidth=0;
        const bool hasMp=labels->mp.length<=4&&measure(labels->mp.text,labels->mp.length,mpWidth);
        const bool hasTp=labels->tp.length<=4&&measure(labels->tp.text,labels->tp.length,tpWidth);
        if(hasMp&&hasTp&&textReference){
            const float breath=.1f,size=(1-breath)*.5f,drop=.05f;
            const float height=static_cast<float>(textReference->height);
            write(labels->tp.text,labels->tp.length,labels->tp.color,size,next,drop*height);
            write(labels->mp.text,labels->mp.length,labels->mp.color,size,next,(drop-size-breath)*height);
            next+=std::max(mpWidth,tpWidth)*size+space;
        }else for(const auto* label:{&labels->mp,&labels->tp}){
            if(label->length>4||!measure(label->text,label->length,width))continue;
            write(label->text,label->length,label->color,.65f,next,0);next+=width*.65f+space;
        }
        single(labels->distance,5);
        if(textReference&&labels->action.length<=MaxLabel&&measure(labels->action.text,labels->action.length,width))
            write(labels->action.text,labels->action.length,labels->action.color,labels->actionScale,
                end-width*labels->actionScale,textReference->height*.3f);
        for(const auto& floating:labels->floating){
            const auto& label=floating.text;
            if(label.length>MaxFloatText||!(floating.alpha>0)||!measure(label.text,label.length,width))continue;
            write(label.text,label.length,label.color,labels->scale,start+(total-width*labels->scale)*.5f,
                (labels->floatUp?-textReference->height*(1.f+floating.drop):textReference->height*(1.2f+floating.drop))*labels->scale,floating.alpha<1?floating.alpha:1);
        }
    }
    if(traits&&traits->bits&&textReference&&validGlyph(in.glyphs[32])){
        const float size=textReference->height*traits->scale;
        const float top=textReference->offsetY+(textReference->height-size)*.5f;
        float right=detailLeft-in.glyphs[32].width;
        const auto alpha=in.nameColor&0xFF000000u;
        const auto quad=[&](float left,float y,float width,float height,unsigned cell,std::uint32_t color){
            auto& q=out.quads[out.count++];
            q.code=0x200u+cell;q.textureGroup=2;q.alphaReference=0;
            for(unsigned v=0;v<4;++v){
                q.vertices[v]={in.x+in.scaleX*(left+(v&1?width:0)),
                    in.y+in.scaleY*(y+(v&2?height:0)),in.z,1,color,
                    cell==7?240.f/256:(cell*32+(v&1?31.5f:.5f))/256,
                    cell==7?.5f:(v&2?31.5f:.5f)/32};
            }
        };
        if(traits->bits&AggroKnown){
            // Quiet separator: one local pixel wide, three quarters of the
            // name height. Its size/position does not follow either detail slider.
            const float height=textReference->height*.75f;
            const float y=textReference->offsetY+(textReference->height-height)*.5f;
            quad(right-2,y-.5f,2,height+1,7,alpha);
            // Half-intensity tint preserves the intended red/blue under native 2x modulation.
            quad(right-1.5f,y,1,height,7,alpha|((traits->bits&Aggressive)?0x6C3C3Cu:0x3C4F62u));
            right-=4;
        }
        for(int flag=6;flag>=0;--flag){
            if(!(traits->bits&(1u<<flag)))continue;
            // Match native neutral RGB under doubled color modulation.
            quad(right-size,top,size,size,static_cast<unsigned>(flag),alpha|0x808080u);
            right-=size+2*traits->scale;
        }
    }
    if(debuffs&&debuffs->count&&debuffs->count<=MaxDebuffs&&textReference){
        const float size=debuffs->size,iconGap=2;
        const float width=debuffs->count*(size+iconGap)-iconGap;
        const float left=-width*.5f;
        float top=static_cast<float>(textReference->offsetY);
        for(unsigned i=0;i<out.count;++i)top=std::min(top,(out.quads[i].vertices[0].y-in.y)/in.scaleY);
        top-=size+2;
        // Native overhead rendering doubles color and alpha modulation.
        // Keep icon opacity fixed, independent of the nameplate fade.
        for(unsigned i=0;i<debuffs->count;++i){
            const auto cell=DebuffCell(debuffs->effects[i]);
            auto& q=out.quads[out.count++];q.code=0x300u+debuffs->effects[i];q.textureGroup=3;q.alphaReference=0;
            for(unsigned v=0;v<4;++v)q.vertices[v]={
                in.x+in.scaleX*(left+i*(size+iconGap)+(v&1?size:0)),
                in.y+in.scaleY*(top+(v&2?size:0)),in.z,1,0x80808080u,
                (cell%16*32+(v&1?31.5f:.5f))/512,(cell/16*32+(v&2?31.5f:.5f))/256};
        }
        if(bounds)*bounds={in.x+in.scaleX*left,in.x+in.scaleX*(left+width),in.y+in.scaleY*top,true};
    }
    return true;
}
}
