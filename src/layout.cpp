#include "layout.h"
#include "debuffs.h"
#include "traits.h"
#include <algorithm>
#include <cmath>

namespace nameplate_lab {
namespace {
bool finite(float value) noexcept { return std::isfinite(value); }
float rounded(double value) noexcept { return static_cast<float>(value); }
constexpr std::uint8_t StatusGlyphs[] = {0x91, 0x9C, 0x92}; // Party, bazaar, linkshell.
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
    bool prefix = icons && icons->replace, keptIcon = false;
    for (unsigned i = 0; i < in.length; ++i) {
        const auto code = in.text[i];
        if (prefix) {
            if (code >= 0x8E) {
                if (code == StatusGlyphs[0] || code == StatusGlyphs[1] || code == StatusGlyphs[2]) continue;
                keptIcon = true;
            } else if (code == 32) {
                if (!keptIcon) continue;
            } else prefix = false;
        }
        if (code >= 0xC8 && code <= 0xCD) {
            const unsigned n = code - 0xC8;
            if (!append(in.expansionBase[n])) return false;
            for (unsigned j = 0; j < in.expansionCount[n]; ++j)
                if (!append(0xAA)) return false;
        } else {
            if (!append(code)) return false;
            if (code == 0xAC && !append(0xAD)) return false;
        }
    }
    nameCount = count;
    if (icons && icons->replace) {
        for (unsigned i = 0; i < 3; ++i)
            if ((icons->active & (1u << i)) && !append(StatusGlyphs[i])) return false;
    }
    return true;
}

bool Build(const Input& in, Output& out, const StatusIcons* icons, const LevelLabel* level, const TraitLabel* traits,
    const DebuffRow* debuffs,DebuffBounds* bounds) noexcept {
    if(bounds)*bounds={};
    out.count = 0;
    if (in.length > MaxGlyphs || !finite(in.x) || !finite(in.y) || !finite(in.z)
        || !finite(in.scaleX) || !finite(in.scaleY) || in.scaleX <= 0 || in.scaleY <= 0
        || in.scaleX > 128 || in.scaleY > 128 || in.z < 0 || in.z >= 1) return false;
    std::uint8_t codes[MaxGlyphs];
    unsigned count = 0, nameCount = 0;
    if (!ExpandName(in, codes, count, nameCount, icons)) return false;
    struct Size { int width, height; } sizes[MaxGlyphs]{};
    int total = 0, iconWidth = 0;
    const Glyph* textReference=nullptr;
    for (unsigned i = 0; i < count; ++i) {
        const auto code = codes[i];
        if (code == 10) continue;
        if (code < 32) return false;
        const auto& g = in.glyphs[code];
        if (!validGlyph(g)) return false;
        if(i<nameCount&&!textReference&&code>32&&code<142)textReference=&g;
        auto& size = sizes[i];
        size = {g.width, g.height};
        if (i >= nameCount) { iconWidth += size.width; continue; }
        if (i && code >= 0x8E && code <= 0xA8) {
            total += size.width / 2;
            size.width = static_cast<int>(static_cast<double>(size.width) * 0.8f);
            size.height = static_cast<int>(static_cast<double>(size.height) * 0.8f);
        } else if (i && code == 0xAA) {
            size.width /= 2;
            size.height /= 2;
            total = static_cast<int>(total + size.width * 0.5);
        } else if (code != 0xAD) total += size.width;
    }
    const float start = rounded(-total * 0.5);
    float pen = start, line = 0;
    const unsigned iconCount = count - nameCount;
    if (iconCount && !in.glyphs[32].valid) return false;
    // Native single-icon size and vertical position, with one loaded-font space
    // between icons and the name. Additional icons grow only toward the left.
    const float gap = iconCount ? static_cast<float>(in.glyphs[32].width) : 0;
    const float iconStart = start - iconWidth - gap * iconCount;
    float detailLeft=iconStart;
    for (unsigned i = 0; i < count; ++i) {
        const auto code = codes[i];
        if (code == 10) { pen = start; line += 8; continue; }
        const bool detached = i >= nameCount;
        if (detached && i == nameCount) { pen = iconStart; line = 0; }
        const auto& g = in.glyphs[code];
        const auto size = sizes[i];
        double scale = 1, advance = size.width;
        int dx = 0, dy = 0;
        if (!detached && i && code >= 0x8E && code <= 0xA8) {
            scale = 0.8f; advance = size.width * 0.625;
            dx = -(size.width / 2); dy = size.height / 2;
        } else if (!detached && i && code == 0xAA) {
            advance = size.width * 0.5; dx = -size.width; dy = size.height;
        } else if (!detached && code == 0xAD) {
            scale = 0.5; advance = 0; dx = -2-size.width; dy = -2;
        }
        const double leftExact = static_cast<double>(g.offsetX + dx) + pen;
        const float left = rounded(leftExact);
        detailLeft=std::min(detailLeft,left);
        const float right = rounded(leftExact + size.width * scale);
        const float top = rounded(static_cast<double>(g.offsetY + dy) + line);
        const float bottom = rounded(static_cast<double>(rounded(size.height * scale)) + top);
        pen = rounded(advance + pen + (detached ? gap : 0));
        auto& q = out.quads[out.count++];
        q.code = code; q.textureGroup = g.textureGroup;
        auto color = g.textureGroup == 1 ? in.shellColor : in.nameColor;
        if (detached && code == 0x92) color = icons->linkshellColor;
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
    if(level&&level->length&&level->length<=6&&nameCount&&validGlyph(in.glyphs[32])){
        float width=0;
        for(unsigned i=0;i<level->length;++i){
            const auto& g=in.glyphs[static_cast<unsigned char>(level->text[i])];
            if(!validGlyph(g))return true; // Optional label unavailable; keep the name.
            width+=g.width;
        }
        float labelPen=iconStart-width*level->scale-in.glyphs[32].width;
        detailLeft=std::min(detailLeft,labelPen);
        const auto& reference=in.glyphs[static_cast<unsigned char>(level->text[0])];
        const float baseline=static_cast<float>(reference.offsetY+reference.height);
        for(unsigned i=0;i<level->length;++i){
            const auto code=static_cast<unsigned char>(level->text[i]);
            const auto& g=in.glyphs[code];
            detailLeft=std::min(detailLeft,labelPen+level->scale*g.offsetX);
            auto& q=out.quads[out.count++];
            q.code=0x100u|code; // Decoration: excluded from name HP bounds/coloring.
            q.textureGroup=g.textureGroup;q.alphaReference=0;
            for(unsigned v=0;v<4;++v){
                q.vertices[v]={
                    in.x+in.scaleX*(labelPen+level->scale*(g.offsetX+(v&1?g.width:0))),
                    in.y+in.scaleY*(baseline+level->scale*(g.offsetY+(v&2?g.height:0)-baseline)),in.z,1,
                    (in.nameColor&0xFF000000u)|level->color,g.uv[v*2],g.uv[v*2+1]};
            }
            labelPen+=g.width*level->scale;
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
