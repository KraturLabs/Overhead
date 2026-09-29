#include "layout.h"
#include "options.h"
#include "debuffs.h"
#include "traits.h"
#include "text_font.h"
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
    out.textInset=in.font?in.font->Pad()*in.font->unit*in.scaleX:0;
    if (in.length > MaxGlyphs || !finite(in.x) || !finite(in.y) || !finite(in.z)
        || !finite(in.scaleX) || !finite(in.scaleY) || in.scaleX <= 0 || in.scaleY <= 0
        || in.scaleX > 128 || in.scaleY > 128 || in.z < 0 || in.z >= 1) return false;
    std::uint8_t codes[MaxGlyphs];
    unsigned count = 0, nameCount = 0;
    if (!ExpandName(in, codes, count, nameCount, icons)) return false;
    const auto custom=[&](unsigned code){return in.font&&code>=text_font::First&&code<=text_font::Last;};
    const float ratio=in.iconRatio>0&&in.iconRatio<=16?in.iconRatio:1;
    // Icon x from its local x, scaled about a fixed anchor so the icon run keeps its attachment.
    const auto iconX=[&](float anchor,float local){return anchor+(local-anchor)*ratio;};
    const auto available=[&](unsigned code){return custom(code)||validGlyph(in.glyphs[code]);};
    const auto glyphAdvance=[&](unsigned code){return custom(code)?in.font->glyphs[code-32].advance*in.font->unit:static_cast<float>(in.glyphs[code].width);};
    const auto kern=[&](unsigned a,unsigned b){return custom(a)&&custom(b)?in.font->kerning[a-32][b-32]*in.font->unit:0.f;};
    const auto textQuad=[&](unsigned code,float pen,float shift,float scale,std::uint32_t color,unsigned tag){
        const auto& f=*in.font;const auto& g=f.glyphs[code-32];
        if(!g.width||!g.height)return; // Spaces advance without a rectangle.
        auto& q=out.quads[out.count++];q.code=tag|code;q.textureGroup=TextTexture;q.alphaReference=0;
        for(unsigned v=0;v<4;++v)q.vertices[v]={
            in.x+in.scaleX*(pen+scale*(g.left+(v&1?g.width:0))*f.unit),
            in.y+in.scaleY*(shift+f.baseline+scale*(g.top+(v&2?g.height:0))*f.unit),in.z,1,color,
            static_cast<float>(g.x+(v&1?g.width:0))/text_font::TextureWidth,
            static_cast<float>(g.y+(v&2?g.height:0))/text_font::TextureHeight};
    };
    // Tag each part the row puts on top; Draw skips the depth test for those.
    const unsigned front=labels?labels->front:0;
    const auto mark=[&](unsigned first,unsigned bit){
        if(front&bit)for(unsigned i=first;i<out.count;++i)out.quads[i].code|=FrontCode;
    };
    struct Size { int width, height; } sizes[MaxGlyphs]{};
    // Name and detached prefix each follow the native rules from their own first
    // glyph: later icons shrink and overlap the one before them. The name is
    // centered on its native width; the prefix ends where the native pen would.
    float total = 0;
    float iconAdvance = 0;
    bool textReference=false;float textHeight=0,textTop=0;
    for (unsigned i = 0; i < count; ++i) {
        const auto code = codes[i];
        if (code == 10) continue;
        if (code < 32) return false;
        const auto& g = in.glyphs[code];
        if (!available(code)) return false;
        if(i<nameCount&&!textReference&&code>32&&code<142){
            textReference=true;textHeight=custom(code)?text_font::CapitalHeight:static_cast<float>(g.height);textTop=custom(code)?0.f:static_cast<float>(g.offsetY);
        }
        if(custom(code)){
            total+=glyphAdvance(code)+(i?kern(codes[i-1],code):0);continue;
        }
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
    if (iconCount && !available(32)) return false;
    // The prefix ends one loaded-font space before the name, as natively.
    const float gap = iconCount ? glyphAdvance(32) : 0;
    const float iconStart = start - iconAdvance - gap;
    float detailLeft=iconStart;
    float nameTop=0;bool nameSeen=false; // Topmost name glyph, local units.
    unsigned iconFirst=0,iconEnd=0; // Detached prefix quads.
    // The atlas holds full 0-255 coverage and the native stage doubles vertex alpha:
    // name alpha above 0x80 only clamps partial edge texels solid, stepping slanted
    // strokes. Cap it at the doubled-modulation neutral; fades below it are unchanged.
    const std::uint32_t letterColor=std::min(in.nameColor>>24,0x80u)<<24|(in.nameColor&0xFFFFFFu);
    for (unsigned i = 0; i < count; ++i) {
        const auto code = codes[i];
        if (code == 10) { pen = start; line += 8; continue; }
        const bool detached = i >= nameCount;
        if (detached && i == nameCount) { pen = iconStart; line = 0; iconFirst = out.count; }
        const unsigned index = detached ? i - nameCount : i;
        if(custom(code)){
            if(index)pen+=kern(codes[i-1],code);
            const auto& g=in.font->glyphs[code-32];
            const float top=in.font->baseline+(g.top+static_cast<int>(in.font->Pad()))*in.font->unit+line;
            if(code>32){
                if(!nameSeen||top<nameTop)nameTop=top;nameSeen=true;
                detailLeft=std::min(detailLeft,pen+g.left*in.font->unit);
            }
            textQuad(code,pen,line,1,letterColor,0);pen+=glyphAdvance(code);continue;
        }
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
        // Detached icons narrow toward the name about the prefix's right end.
        const float anchor=start-gap;
        const float left = detached?iconX(anchor,rounded(leftExact)):rounded(leftExact);
        detailLeft=std::min(detailLeft,left);
        const float right = detached?iconX(anchor,rounded(leftExact + size.width * scale)):rounded(leftExact + size.width * scale);
        const float top = rounded(static_cast<double>(g.offsetY + dy) + line);
        const float bottom = rounded(static_cast<double>(rounded(size.height * scale)) + top);
        if(!detached&&(!nameSeen||top<nameTop)){nameTop=top;nameSeen=true;}
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
    if(iconCount)iconEnd=out.count;
    mark(0,RowShow);
    // Text runs: decorations (0x100|code) stay out of HP bounds/coloring.
    // Scale grows from the run's own baseline; shift moves it down in local units.
    // '%' is drawn smaller than the digits it follows, on the same baseline.
    constexpr float Symbol=.5f;
    // Native overhead rendering doubles color and alpha modulation. Only the name
    // follows the native target pulse (its alpha); every detail uses this fixed opacity.
    constexpr std::uint32_t DetailAlpha=0x80000000u;
    const auto measure=[&](const char* text,unsigned length,float& width){
        width=0;
        for(unsigned i=0;i<length;++i){
            if(!available(static_cast<unsigned char>(text[i])))return false;
            width+=(glyphAdvance(static_cast<unsigned char>(text[i]))+(i?kern(static_cast<unsigned char>(text[i-1]),static_cast<unsigned char>(text[i])):0))*(text[i]=='%'?Symbol:1);
        }
        return length>0;
    };
    const auto write=[&](const char* text,unsigned length,std::uint32_t color,float scale,float pen,float shift,float alpha=1){
        const auto opacity=static_cast<std::uint32_t>(static_cast<float>(DetailAlpha>>24)*alpha)<<24;
        const auto& reference=in.glyphs[static_cast<unsigned char>(text[0])];
        const float baseline=in.font?in.font->baseline:static_cast<float>(reference.offsetY+reference.height);
        float leftmost=pen;
        for(unsigned i=0;i<length;++i){
            const auto code=static_cast<unsigned char>(text[i]);
            const auto& g=in.glyphs[code];
            const float size=code=='%'?scale*Symbol:scale;
            if(custom(code)){
                if(i)pen+=kern(static_cast<unsigned char>(text[i-1]),code)*size;
                leftmost=std::min(leftmost,pen+size*in.font->glyphs[code-32].left*in.font->unit);
                textQuad(code,pen,shift,size,opacity|color,0x100u);pen+=glyphAdvance(code)*size;continue;
            }
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
    if(level&&(level->reserve||level->length)&&level->length<=6&&nameCount&&available(32)){
        // "Lv." (60% size) and the number are white like the distance; only the check rank carries the color.
        // With reserve, the number sits left-aligned in a fixed "Lv.00"/"Lv.??" slot so details
        // left of it do not move as the level arrives; only 3-digit levels widen it.
        constexpr unsigned Prefix=3;constexpr float PrefixSize=.6f;
        const bool shown=level->length>Prefix;
        float prefixWidth=0,width=0,digits=0,unknown=0;
        if(!measure("Lv.",Prefix,prefixWidth)||(shown&&!measure(level->text+Prefix,level->length-Prefix,width)))return true;
        if(level->reserve){
            if(measure("00",2,digits))width=std::max(width,digits);
            if(measure("??",2,unknown))width=std::max(width,unknown);
        }
        const float space=glyphAdvance(32);
        const float labelPen=iconStart-(prefixWidth*PrefixSize+width)*level->scale-space;
        detailLeft=std::min(detailLeft,labelPen);
        if(shown){
            const auto first=out.count;
            detailLeft=std::min(detailLeft,write(level->text,Prefix,0x808080u,level->scale*PrefixSize,labelPen,0));
            // The number sits a little right of "Lv." (fixed, in name units).
            constexpr float NumberGap=.7f;
            write(level->text+Prefix,level->length-Prefix,0x808080u,level->scale,labelPen+prefixWidth*PrefixSize*level->scale+NumberGap,0);
            if(level->checkLength&&level->checkLength<=3&&nameSeen){
                // Check rank centered over the small "Lv.", sitting on its top, number color.
                // Sized 75% past the room below the name's top (at most 80% of the level size);
                // glyph cells carry transparent top padding, so the visible letters still sit near the name's top.
                const auto extent=[&](const char* text,unsigned length,float& top){
                    if(custom(static_cast<unsigned char>(text[0]))){
                        top=in.font->baseline;
                        for(unsigned i=0;i<length;++i){
                            const auto code=static_cast<unsigned char>(text[i]);
                            if(custom(code))top=std::min(top,in.font->baseline+(in.font->glyphs[code-32].top+static_cast<int>(in.font->Pad()))*in.font->unit);
                        }
                        return in.font->baseline;
                    }
                    top=static_cast<float>(in.glyphs[static_cast<unsigned char>(text[0])].offsetY);
                    for(unsigned i=1;i<length;++i)top=std::min(top,static_cast<float>(in.glyphs[static_cast<unsigned char>(text[i])].offsetY));
                    const auto& r=in.glyphs[static_cast<unsigned char>(text[0])];
                    return static_cast<float>(r.offsetY+r.height);
                };
                float checkWidth=0,prefixTop=0,checkTop=0;
                if(measure(level->check,level->checkLength,checkWidth)){
                    const float prefixBase=extent(level->text,Prefix,prefixTop);
                    const float checkBase=extent(level->check,level->checkLength,checkTop);
                    const float bottom=prefixBase+level->scale*PrefixSize*(prefixTop-prefixBase);
                    const float height=checkBase-checkTop;
                    const float size=height>0?std::min(level->scale*.8f,1.75f*(bottom-nameTop)/height):0;
                    if(size>0){
                        // Tuned in game: a little right of centre and up (name units).
                        constexpr float CheckX=.6f,CheckY=-.8f;
                        const float checkPen=labelPen+(prefixWidth*PrefixSize*level->scale-checkWidth*size)/2+CheckX;
                        detailLeft=std::min(detailLeft,write(level->check,level->checkLength,level->color,size,checkPen,bottom-checkBase+CheckY));
                    }
                }
            }
            mark(first,ShowLevel);
        }
    }
    // One quad from the trait atlas: 32 px cells, 8 per row. Cell 7 is solid white;
    // cells 8-19 are the damage modifier icons.
    const auto atlasQuad=[&](float left,float y,float width,float height,unsigned cell,std::uint32_t color){
        auto& q=out.quads[out.count++];
        q.code=0x200u+cell;q.textureGroup=2;q.alphaReference=0;
        for(unsigned v=0;v<4;++v){
            q.vertices[v]={in.x+in.scaleX*(left+(v&1?width:0)),
                in.y+in.scaleY*(y+(v&2?height:0)),in.z,1,color,
                cell==7?240.f/256:(cell%8*32+(v&1?31.5f:.5f))/256,
                (cell==7?16.f:cell/8*32+(v&2?31.5f:.5f))/TraitTextureHeight};
        }
    };
    // Column slots in name heights from the name's middle: three values at 35% with 3% gaps,
    // two at 46.5%, both centred 5% below the middle; a lone value sits on the baseline at 60%.
    constexpr float RowGap=.03f,Offset=.05f,Three=.35f,Two=.465f,Single=.6f;
    // Extra space between stacked rows, in name units (tuned in game across fonts).
    constexpr float TwoRowSpace=.3f,ThreeRowSpace=.2f;
    if(labels&&nameCount&&available(32)){
        // Weaknesses/resistances, HP%, MP, TP then distance follow the name's advance; the action
        // overlaps its lower right corner. None shift the name, level, traits or cursor.
        const float space=glyphAdvance(32),end=start+total;
        float next=end+space,width=0;
        const auto single=[&](const TextLabel& label,unsigned limit,unsigned bit){
            if(label.length>limit||!measure(label.text,label.length,width))return;
            const auto first=out.count;
            write(label.text,label.length,label.color,labels->scale,next,0);next+=width*labels->scale+space;
            mark(first,bit);
        };
        // Weaknesses over resistances first after the name, in the two-value HP/MP/TP slots.
        // A lone row mirrors the traits instead: trait-sized icons centred on the name, led by
        // a bar shaped and placed like the aggression bar. Bars: green weak, red resist.
        if(textReference&&(labels->weak||labels->resist)){
            const float height=static_cast<float>(textHeight);
            const float middle=textTop+height*.5f;
            const bool both=labels->weak&&labels->resist;
            const float rowGap=RowGap+(height>0?TwoRowSpace/height:0);
            float top=Offset-(2*Two+rowGap)*.5f,column=0;
            const struct{std::uint16_t mask;float scale;std::uint32_t tint;unsigned bit;} sets[2]={
                {labels->weak,labels->weakScale,0x3C6C3Cu,ShowWeak},{labels->resist,labels->resistScale,0x6C3C3Cu,ShowResist}};
            for(const auto& set:sets){
                if(!set.mask)continue;
                const auto first=out.count;
                const float size=(both?Two:labels->traitScale)*height*set.scale;
                const float y=both?middle+(top+Two*.5f)*height-size*.5f:middle-size*.5f;
                const float barHeight=both?size:height*.5625f,barY=both?y:middle-barHeight*.5f+height*.05f;
                const float iconGap=both?size*.1f:2*labels->traitScale*set.scale;
                // Half-intensity tints under native 2x modulation, like the aggression bar.
                // Icon widths and gaps follow the icon ratio from the bar's left edge.
                atlasQuad(next,barY-.5f,2*ratio,barHeight+1,7,DetailAlpha);
                atlasQuad(iconX(next,next+.5f),barY,ratio,barHeight,7,DetailAlpha|set.tint);
                float iconPen=next+(both?2+iconGap:4);
                for(unsigned m=0;m<ModifierCount;++m){
                    if(!(set.mask&(1u<<m)))continue;
                    atlasQuad(iconX(next,iconPen),y,size*ratio,size,8+m,DetailAlpha|0x808080u);
                    iconPen+=size+iconGap;
                }
                column=std::max(column,(iconPen-iconGap-next)*ratio);
                mark(first,set.bit);
                top+=Two+rowGap;
            }
            next+=column+space*.5f; // HP/MP/TP follow half a space after.
        }
        // HP over MP over TP in one column centered on the name's middle, nudged down 5%:
        // three at 35% of the name height with 3% gaps, two at 46.5%, both around the same centre.
        // A lone value sits on the name's baseline at 60%.
        if(textReference){
            const float height=static_cast<float>(textHeight);
            const TextLabel* present[3]{};float widths[3]{};unsigned bits[3]{},rowCount=0;
            const struct{const TextLabel* label;unsigned bit;} rows[3]={
                {&labels->health,ShowHealth},{&labels->mp,ShowMp},{&labels->tp,ShowTp}};
            for(const auto& row:rows)
                if(row.label->length<=4&&measure(row.label->text,row.label->length,widths[rowCount])){
                    present[rowCount]=row.label;bits[rowCount++]=row.bit;
                }
            const float rowGap=RowGap+(height>0?(rowCount==2?TwoRowSpace:rowCount==3?ThreeRowSpace:0)/height:0);
            float top=Offset-(rowCount==2?2*Two+rowGap:3*Three+2*rowGap)*.5f,column=0; // Name heights from the name's middle.
            for(unsigned i=0;i<rowCount;++i){
                const float size=rowCount==1?Single:rowCount==2?Two:Three;
                const auto first=out.count;
                write(present[i]->text,present[i]->length,present[i]->color,size,next,rowCount==1?0:(top+size-.5f)*height);
                mark(first,bits[i]);
                column=std::max(column,widths[i]*size);
                top+=size+rowGap;
            }
            if(column>0)next+=column+space*.5f; // Distance follows half a space after the column.
        }
        single(labels->distance,5,ShowDistance);
        if(textReference&&labels->action.length<=MaxLabel&&measure(labels->action.text,labels->action.length,width)){
            const auto first=out.count;
            write(labels->action.text,labels->action.length,labels->action.color,labels->actionScale,
                end-width*labels->actionScale,textHeight*.2f);
            mark(first,ShowAction);
        }
        constexpr float XpSize=.60f; // Scrolling points keep their own size, independent of distance.
        for(const auto& floating:labels->floating){
            const auto& label=floating.text;
            if(label.length>MaxFloatText||!(floating.alpha>0))continue;
            // Up to four runs: text, a small lowered chain count, text, a half-size raised unit.
            // Each run takes its first glyph's baseline, so runs must not start with a space.
            constexpr float SubSize=.6f,SubDrop=.2f,UnitSize=.5f,UnitRise=.45f;
            const unsigned unitStart=floating.unitStart<label.length?floating.unitStart:label.length;
            const unsigned superStart=floating.superLength&&floating.superStart+floating.superLength<=unitStart?floating.superStart:unitStart;
            const unsigned ends[4]={superStart,superStart+(superStart<unitStart?floating.superLength:0),unitStart,label.length};
            const float runSizes[4]={XpSize,XpSize*SubSize,XpSize,XpSize*UnitSize};
            float widths[4]{};bool ok=true;
            for(unsigned r=0,from=0;r<4;from=ends[r++])
                if(ends[r]>from&&!measure(label.text+from,ends[r]-from,widths[r]))ok=false;
            if(!ok)continue;
            width=0;for(unsigned r=0;r<4;++r)width+=widths[r]*runSizes[r];
            const float shift=textHeight*(1.2f+floating.drop)*XpSize;
            const auto first=out.count;
            float runPen=start+(total-width)*.5f;
            for(unsigned r=0,from=0;r<4;from=ends[r++]){
                if(ends[r]==from)continue;
                write(label.text+from,ends[r]-from,label.color,runSizes[r],runPen,
                    shift+textHeight*XpSize*(r==1?SubDrop:r==3?-UnitRise:0.f),floating.alpha<1?floating.alpha:1);
                runPen+=widths[r]*runSizes[r];
            }
            // Scrolling point gains always draw on top.
            for(auto i=first;i<out.count;++i)out.quads[i].code|=FrontCode;
        }
    }
    if(traits&&traits->bits&&textReference&&available(32)){
        const float size=textHeight*traits->scale;
        const float top=textTop+(textHeight-size)*.5f;
        float right=detailLeft-glyphAdvance(32);
        const auto alpha=DetailAlpha;
        const auto first=out.count;
        // Traits run leftward from the first slot; widths and gaps follow the icon ratio.
        const float anchor=right;
        const auto quad=[&](float left,float y,float width,float height,unsigned cell,std::uint32_t color){
            atlasQuad(iconX(anchor,left),y,width*ratio,height,cell,color);
        };
        if(traits->bits&AggroKnown){
            // Quiet separator: one local pixel wide, 56% of the name height, centered
            // like the HP/MP/TP column (cell middle nudged down 5%). Not slider-sized.
            const float height=textHeight*.5625f;
            const float y=textTop+(textHeight-height)*.5f+textHeight*.05f;
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
        mark(first,ShowTraits);
    }
    if(debuffs&&debuffs->count&&debuffs->count<=MaxDebuffs&&textReference){
        const float size=debuffs->size,iconGap=2;
        const float width=(debuffs->count*(size+iconGap)-iconGap)*ratio;
        const float left=-width*.5f;
        float top=static_cast<float>(textTop);
        // Taller detached name icons sit off to the left; they do not lift the row.
        for(unsigned i=0;i<out.count;++i)if(i<iconFirst||i>=iconEnd)top=std::min(top,(out.quads[i].vertices[0].y-in.y)/in.scaleY);
        top-=size+1;
        for(unsigned i=0;i<debuffs->count;++i){
            const auto cell=DebuffCell(debuffs->effects[i]);
            auto& q=out.quads[out.count++];q.code=0x300u+debuffs->effects[i];q.textureGroup=3;q.alphaReference=0;
            for(unsigned v=0;v<4;++v)q.vertices[v]={
                in.x+in.scaleX*(left+(i*(size+iconGap)+(v&1?size:0))*ratio),
                in.y+in.scaleY*(top+(v&2?size:0)),in.z,1,DetailAlpha|0x808080u,
                (cell%16*32+(v&1?31.5f:.5f))/512,(cell/16*32+(v&2?31.5f:.5f))/256};
        }
        mark(out.count-debuffs->count,ShowDebuffs);
        if(bounds)*bounds={in.x+in.scaleX*left,in.x+in.scaleX*(left+width),in.y+in.scaleY*top,true};
    }
    return true;
}
}
