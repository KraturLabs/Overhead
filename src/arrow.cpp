#include "arrow.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace overhead {
namespace {
#include "arrow_atlas.inc"
constexpr float Pi=3.14159265358979f;
// Source canvas geometry from the approved reference (300x300, tip at 150,280).
constexpr float GemPolygon[4][2]={{150,12},{204,76},{150,135},{99,76}};
// The gem cells cover this 150x150 canvas square at the same 128/300 scale.
constexpr float GemLeft=75,GemTop=0,GemSpan=150;
constexpr unsigned GemCell=64;

std::uint32_t Premultiply(std::uint32_t argb) noexcept {
    const unsigned a=argb>>24;
    const auto scale=[a](unsigned c){return (c*a+127)/255;};
    return (a<<24)|(scale(argb>>16&255)<<16)|(scale(argb>>8&255)<<8)|scale(argb&255);
}
std::uint32_t Scaled(std::uint32_t pm,float f) noexcept {
    const auto c=[f](unsigned v){return static_cast<unsigned>(std::lround(v*f));};
    return (c(pm>>24)<<24)|(c(pm>>16&255)<<16)|(c(pm>>8&255)<<8)|c(pm&255);
}
bool InsideGem(float x,float y) noexcept {
    // Convex, clockwise in screen space: inside when on the same side of every edge.
    for(unsigned i=0;i<4;++i){
        const auto* a=GemPolygon[i];const auto* b=GemPolygon[(i+1)%4];
        if((b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0])<0)return false;
    }
    return true;
}
// Fraction of a cell texel (canvas units per texel: step) inside the gem, 4x4 samples.
float GemCoverage(float left,float top,float step) noexcept {
    unsigned hits=0;
    for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx)
        hits+=InsideGem(left+(sx+.5f)*step/4,top+(sy+.5f)*step/4);
    return hits/16.f;
}
float Mix(float a,float b,float t) noexcept {return a+(b-a)*t;}
float Smooth(float x) noexcept {x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
std::uint32_t Color(float r,float g,float b,float a) noexcept {
    // Premultiplied diffuse: the texture stage multiplies it with premultiplied texels.
    a=std::clamp(a,0.f,1.f);
    const auto c=[a](float v){return static_cast<std::uint32_t>(std::lround(std::clamp(v,0.f,1.f)*a*255));};
    return (static_cast<std::uint32_t>(std::lround(a*255))<<24)|(c(r)<<16)|(c(g)<<8)|c(b);
}
struct Cell { float u0,v0,u1,v1,span; };
constexpr float AtlasU(float px) noexcept {return px/ArrowAtlasWidth;}
constexpr float AtlasV(float px) noexcept {return px/ArrowAtlasHeight;}
}

bool BuildArrowAtlas(std::uint32_t* pixels) noexcept {
    constexpr unsigned cell=ArrowCell;
    static_assert(sizeof(ArrowFrames)/sizeof(*ArrowFrames)==2*ArrowCell*ArrowCell,"two arrow frames");
    if(!pixels)return false;
    std::memset(pixels,0,ArrowAtlasWidth*ArrowAtlasHeight*4);
    const auto at=[&](unsigned x,unsigned y)->std::uint32_t&{return pixels[y*ArrowAtlasWidth+x];};
    const float step=300.f/cell; // Canvas units per texel.
    for(unsigned frame=0;frame<2;++frame){
        const auto* source=ArrowFrames+frame*cell*cell;
        for(unsigned y=0;y<cell;++y)for(unsigned x=0;x<cell;++x){
            // A clear margin keeps filtering (down to the smallest mip) from pulling
            // resampling residue into neighbouring cells. The art stays inside:
            // canvas 9..283 tall (gem top 12, tip 280) and 19..281 wide.
            const bool margin=y<4||y>=121||x<8||x>=120;
            const auto pm=margin?0u:Premultiply(source[y*cell+x]);
            const float cover=GemCoverage(x*step,y*step,step);
            at(frame*cell+x,y)=pm;
            at(256+frame*cell+x,y)=Scaled(pm,1-cover);
        }
        // The gem cell samples the same frame inside the diamond only.
        for(unsigned y=0;y<GemCell;++y)for(unsigned x=0;x<GemCell;++x){
            const float cx=GemLeft+x*step,cy=GemTop+y*step;
            const unsigned sx=static_cast<unsigned>(cx/step),sy=static_cast<unsigned>(cy/step);
            if(sx>=cell||sy>=cell)continue;
            at(frame*GemCell+x,cell+y)=Scaled(Premultiply(source[sy*cell+sx]),GemCoverage(cx,cy,step));
        }
    }
    // Soft round glow and a solid white patch for the procedural trail.
    for(unsigned y=0;y<GemCell;++y)for(unsigned x=0;x<GemCell;++x){
        const float dx=(x+.5f)/GemCell*2-1,dy=(y+.5f)/GemCell*2-1;
        const float d=std::sqrt(dx*dx+dy*dy);
        const float a=d>=1?0:(1-d)*(1-d);
        const auto v=static_cast<std::uint32_t>(std::lround(a*255));
        at(128+x,cell+y)=(v<<24)|(v<<16)|(v<<8)|v;
    }
    // The white patch sits apart from the glow so filtering never mixes them.
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)at(224+x,136+y)=0xFFFFFFFFu;
    return true;
}

void ShrinkArrowLevel(const std::uint32_t* source,unsigned width,unsigned height,std::uint32_t* target) noexcept {
    const unsigned w=width/2,h=height/2;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        unsigned sum[4]{};
        for(unsigned k=0;k<4;++k){
            const auto p=source[(y*2+k/2)*width+x*2+k%2];
            for(unsigned c=0;c<4;++c)sum[c]+=p>>(c*8)&255;
        }
        std::uint32_t out=0;
        for(unsigned c=0;c<4;++c)out|=((sum[c]+2)/4)<<(c*8);
        target[y*w+x]=out;
    }
}

float ArrowShimmerWeight(double millis) noexcept {
    constexpr unsigned steps=sizeof(ArrowShimmer)/sizeof(*ArrowShimmer);
    if(!std::isfinite(millis)||millis<0)return 0;
    const double position=std::fmod(millis,steps*40.0)/40.0;
    const auto i=static_cast<unsigned>(position)%steps;
    return Mix(ArrowShimmer[i],ArrowShimmer[(i+1)%steps],static_cast<float>(position-std::floor(position)));
}

ArrowPose ArrowMotion::Update(const ArrowKey& key,float x,float y,double millis,float glideMillis,bool glide) noexcept {
    // A long gap (hidden arrow, menus, zoning) starts fresh at the target.
    if(!known_||millis-seen_>250||millis<seen_){known_=true;moving_=false;key_=key;}
    else if(!(key==key_)){
        key_=key;
        const float dx=x-last_.x,dy=y-last_.y;
        moving_=glide&&glideMillis>0&&dx*dx+dy*dy>4;
        if(moving_){
            from_=last_;started_=millis;angle_=std::atan2(dy,dx);
            // Turn the short way from wherever the previous glide left the arrow.
            float turn=angle_-Pi/2;
            while(turn-from_.rotation>Pi)turn-=2*Pi;
            while(turn-from_.rotation<-Pi)turn+=2*Pi;
            angle_=turn+Pi/2;
        }
    }
    ArrowPose pose{x,y,0,0,0};
    if(moving_){
        const float t=std::clamp(static_cast<float>((millis-started_)/glideMillis),0.f,1.f);
        const float ease=.5f-.5f*std::cos(Pi*t);
        const float launch=Smooth(t/.22f),landing=Smooth((t-.72f)/.28f);
        pose.x=Mix(from_.x,x,ease);pose.y=Mix(from_.y,y,ease);
        pose.morph=Mix(from_.morph,1,launch)*(1-landing);
        pose.rotation=Mix(from_.rotation,angle_-Pi/2,launch)*(1-landing);
        pose.angle=angle_;
        if(t>=1)moving_=false;
    }
    last_=pose;seen_=millis;
    return pose;
}

bool BuildArrow(const ArrowPose& p,float size,ArrowGeometry& out) noexcept {
    out.trail=out.body=out.glow=out.gem=0;
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(size)||size<=0||size>2048)return false;
    const float s=size/300,k=size/84; // Canvas scale; the reference's 84 px preview units.
    const float m=std::clamp(p.morph,0.f,1.f);
    unsigned count=0;
    const auto vertex=[&](float x,float y,std::uint32_t color,float u0,float v0,float u1,float v1){
        if(count<ArrowGeometry::Capacity)out.vertices[count++]={x-.5f,y-.5f,0,1,color,u0,v0,u1,v1};
    };
    // One textured quad from four transformed corners (canvas corners given).
    const auto quad=[&](const float (&c)[4][2],std::uint32_t color,const Cell& cell){
        const float us[4]={0,1,1,0},vs[4]={0,0,1,1};
        const unsigned order[6]={0,1,2,0,2,3};
        for(auto i:order)vertex(c[i][0],c[i][1],color,cell.u0+us[i]*cell.span/ArrowAtlasWidth,cell.v0+vs[i]*cell.span/ArrowAtlasHeight,
            cell.u1+us[i]*cell.span/ArrowAtlasWidth,cell.v1+vs[i]*cell.span/ArrowAtlasHeight);
    };
    const Cell full{0,0,AtlasU(128),0,128},body{AtlasU(256),0,AtlasU(384),0,128};
    const Cell gem{0,AtlasV(128),AtlasU(64),AtlasV(128),64},glowCell{AtlasU(128),AtlasV(128),AtlasU(128),AtlasV(128),64};
    const float whiteU=AtlasU(232),whiteV=AtlasV(144);
    // Canvas point -> screen for a part placed at (tx,ty), rotated, scaled about a pivot.
    const auto place=[](float tx,float ty,float r,float sx,float sy,float px,float py,float cx,float cy,float (&o)[2]){
        const float x=(cx-px)*sx,y=(cy-py)*sy,cs=std::cos(r),sn=std::sin(r);
        o[0]=tx+cs*x-sn*y;o[1]=ty+sn*x+cs*y;
    };
    if(m<.001f){
        float c[4][2];
        const float corners[4][2]={{0,0},{300,0},{300,300},{0,300}};
        for(unsigned i=0;i<4;++i){c[i][0]=p.x+(corners[i][0]-150)*s;c[i][1]=p.y+(corners[i][1]-280)*s;}
        quad(c,Color(1,1,1,1),full);
        out.body=count;
        return true;
    }
    const float dx=std::cos(p.angle),dy=std::sin(p.angle);
    // Light ribbon in the travel frame, centred 34 units above the tip.
    const float ox=p.x,oy=p.y-34*k,cs=dx,sn=dy;
    const auto trailPoint=[&](float lx,float ly,float (&o)[2]){o[0]=ox+cs*lx*k-sn*ly*k;o[1]=oy+sn*lx*k+cs*ly*k;};
    const auto fillAlpha=[&](float lx){ // Linear gradient -76..12: 0, .34m at .65, .8m at the head.
        const float t=std::clamp((lx+76)/88,0.f,1.f);
        return t<.65f?Mix(0,.34f*m,t/.65f):Mix(.34f*m,.8f*m,(t-.65f)/.35f);
    };
    const auto fillColor=[&](float lx,float alpha){
        const float t=std::clamp((lx+76)/88,0.f,1.f),w=t<.65f?0:(t-.65f)/.35f;
        return Color(1,Mix(205,245,w)/255,Mix(86,209,w)/255,alpha);
    };
    constexpr unsigned Segments=12;
    const auto lens=[&](float spread,float glowScale){
        // Two quadratic edges -76..12 bulging to +-9m; spread widens it for the glow.
        for(unsigned i=0;i<Segments;++i){
            float pt[2][2],pb[2][2],pc[2][2];float alpha[2],lx[2];
            for(unsigned j=0;j<2;++j){
                const float t=static_cast<float>(i+j)/Segments;
                lx[j]=(1-t)*(1-t)*-76+2*(1-t)*t*-25+t*t*12;
                const float half=2*(1-t)*t*9*m+spread;
                trailPoint(lx[j],-half,pt[j]);trailPoint(lx[j],half,pb[j]);trailPoint(lx[j],0,pc[j]);
                alpha[j]=glowScale?fillAlpha(lx[j])*glowScale:fillAlpha(lx[j]);
            }
            const auto edge=glowScale?Color(1,201.f/255,74.f/255,0):0;
            const auto c0=glowScale?Color(1,201.f/255,74.f/255,alpha[0]):fillColor(lx[0],alpha[0]);
            const auto c1=glowScale?Color(1,201.f/255,74.f/255,alpha[1]):fillColor(lx[1],alpha[1]);
            const auto e0=glowScale?edge:c0,e1=glowScale?edge:c1;
            // Top half then bottom half; the glow fades to nothing at its outer edge.
            vertex(pt[0][0],pt[0][1],e0,whiteU,whiteV,whiteU,whiteV);vertex(pt[1][0],pt[1][1],e1,whiteU,whiteV,whiteU,whiteV);vertex(pc[1][0],pc[1][1],c1,whiteU,whiteV,whiteU,whiteV);
            vertex(pt[0][0],pt[0][1],e0,whiteU,whiteV,whiteU,whiteV);vertex(pc[1][0],pc[1][1],c1,whiteU,whiteV,whiteU,whiteV);vertex(pc[0][0],pc[0][1],c0,whiteU,whiteV,whiteU,whiteV);
            vertex(pc[0][0],pc[0][1],c0,whiteU,whiteV,whiteU,whiteV);vertex(pc[1][0],pc[1][1],c1,whiteU,whiteV,whiteU,whiteV);vertex(pb[1][0],pb[1][1],e1,whiteU,whiteV,whiteU,whiteV);
            vertex(pc[0][0],pc[0][1],c0,whiteU,whiteV,whiteU,whiteV);vertex(pb[1][0],pb[1][1],e1,whiteU,whiteV,whiteU,whiteV);vertex(pb[0][0],pb[0][1],e0,whiteU,whiteV,whiteU,whiteV);
        }
    };
    lens(10*m,.7f/.8f); // Soft gold glow around the ribbon (canvas shadowBlur 10m).
    lens(0,0);
    {   // Bright core line -55..12, 1.5 units wide.
        float a[2],b[2],c[2],d[2];
        trailPoint(-55,-.75f,a);trailPoint(12,-.75f,b);trailPoint(12,.75f,c);trailPoint(-55,.75f,d);
        const auto col=Color(1,239.f/255,193.f/255,.8f*m);
        for(const float* q:{a,b,c,a,c,d})vertex(q[0],q[1],col,whiteU,whiteV,whiteU,whiteV);
    }
    out.trail=count;
    // Body: folds to a narrow streak behind, rotated toward travel.
    {
        const float tx=p.x-22*dx*m*k,ty=p.y+Mix(-95*s,(-34-22*dy)*k,m);
        const float corners[4][2]={{0,0},{300,0},{300,300},{0,300}};
        float c[4][2];
        for(unsigned i=0;i<4;++i)place(tx,ty,p.rotation,s*Mix(1,.18f,m),s*Mix(1,1.2f,m),150,185,corners[i][0],corners[i][1],c[i]);
        quad(c,Color(1,1,1,1-.3f*m),body);
    }
    out.body=count-out.trail;
    // Diamond leads, with a warm glow behind it.
    const float gx=p.x+24*dx*m*k,gy=p.y+Mix(-205*s,(-34+24*dy)*k,m),gs=s*Mix(1,.86f,m);
    {
        const float blur=12*m*k,halfW=(204-99)/2.f*gs+blur,halfH=(135-12)/2.f*gs+blur;
        const float cyOffset=((12+135)/2.f-75)*gs;
        float c[4][2];
        const float local[4][2]={{-halfW,-halfH},{halfW,-halfH},{halfW,halfH},{-halfW,halfH}};
        for(unsigned i=0;i<4;++i){
            const float x=local[i][0],y=local[i][1]+cyOffset,co=std::cos(p.rotation),si=std::sin(p.rotation);
            c[i][0]=gx+co*x-si*y;c[i][1]=gy+si*x+co*y;
        }
        const auto before=count;
        quad(c,Color(1,218.f/255,126.f/255,.65f*m),glowCell);
        out.glow=count-before;
    }
    {
        const float corners[4][2]={{GemLeft,GemTop},{GemLeft+GemSpan,GemTop},{GemLeft+GemSpan,GemTop+GemSpan},{GemLeft,GemTop+GemSpan}};
        float c[4][2];
        for(unsigned i=0;i<4;++i)place(gx,gy,p.rotation,gs,gs,150,75,corners[i][0],corners[i][1],c[i]);
        const auto before=count;
        quad(c,Color(1,1,1,1),gem);
        out.gem=count-before;
    }
    return count<=ArrowGeometry::Capacity&&out.trail+out.body+out.glow+out.gem==count;
}
}
