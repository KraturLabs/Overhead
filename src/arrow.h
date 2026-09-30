#pragma once
#include <cstdint>

namespace overhead {
// Custom target arrow: the approved gold artwork at rest, and a diamond-led gold
// streak while it travels between targets. Pure math and pixels; no D3D here.

// Atlas layout, level 0 (premultiplied ARGB): four 128 px cells on the top row
// (calm, peak, calm body, peak body), then 64 px cells (calm gem, peak gem, glow) and a
// 16 px white patch at 224,136 for the trail.
constexpr unsigned ArrowAtlasWidth=512,ArrowAtlasHeight=256,ArrowAtlasLevels=4;
bool BuildArrowAtlas(std::uint32_t* pixels) noexcept; // ArrowAtlasWidth*ArrowAtlasHeight
// Box-filters one premultiplied level into the next (half size in each direction).
void ShrinkArrowLevel(const std::uint32_t* source,unsigned width,unsigned height,std::uint32_t* target) noexcept;
// Peak weight of the 1.92 s rest shimmer at a time in milliseconds.
float ArrowShimmerWeight(double millis) noexcept;

// The on-screen pose: x,y is the tip (the 300 px source canvas point 150,280).
struct ArrowPose { float x=0,y=0,morph=0,rotation=0,angle=0; };
// Identity of the selected target; a change starts a glide from the current pose.
struct ArrowKey {
    std::uint32_t id=0,index=0,actor=0;
    bool operator==(const ArrowKey& o) const noexcept {return id==o.id&&index==o.index&&actor==o.actor;}
};
class ArrowMotion {
public:
    // Call once per drawn frame with the live tip. Selection itself is never delayed.
    ArrowPose Update(const ArrowKey& key,float x,float y,double millis,float glideMillis,bool glide) noexcept;
    void Reset() noexcept {*this=ArrowMotion{};}
    bool Moving() const noexcept {return moving_;}
private:
    bool known_=false,moving_=false;
    ArrowKey key_;
    ArrowPose last_,from_;
    double seen_=0,started_=0;
    float angle_=0;
};

// Vertices carry two texture coordinates: calm (u0,v0) and peak (u1,v1).
struct ArrowVertex { float x,y,z,rhw; std::uint32_t color; float u0,v0,u1,v1; };
// Draw order: trail (plain), body (shimmer), gem glow (plain), gem (shimmer).
struct ArrowGeometry {
    static constexpr unsigned Capacity=320; // Trail 294, body, glow and gem 6 each.
    ArrowVertex vertices[Capacity];
    unsigned trail=0,body=0,glow=0,gem=0; // Vertex counts, triangle lists, in draw order.
};
// size: on-screen pixels for the 300 px source canvas.
bool BuildArrow(const ArrowPose& pose,float size,ArrowGeometry& out) noexcept;
}
