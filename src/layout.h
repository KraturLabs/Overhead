#pragma once
#include <cstdint>

namespace nameplate_lab {
constexpr unsigned MaxGlyphs = 36;
struct Glyph {
    std::int16_t width, height, offsetX, offsetY;
    float uv[8];
    std::uint8_t valid, textureGroup;
    std::uint16_t reserved;
};
struct Input {
    std::uint32_t length;
    std::uint8_t text[MaxGlyphs];
    float x, y, z, scaleX, scaleY;
    std::uint32_t nameColor, shellColor;
    std::uint8_t expansionBase[6], expansionCount[6];
    Glyph glyphs[256];
};
struct Vertex {
    float x, y, z, rhw;
    std::uint32_t color;
    float u, v;
};
struct Quad {
    std::uint32_t code, textureGroup, alphaReference;
    Vertex vertices[4];
};
struct Output {
    std::uint32_t count;
    Quad quads[MaxGlyphs];
};
static_assert(sizeof(Glyph) == 44 && sizeof(Vertex) == 28 && sizeof(Quad) == 124);
// Pure layout: no pointers, graphics calls, allocations, or changes to game state.
bool Build(const Input& input, Output& output) noexcept;
}
