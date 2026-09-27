#pragma once
#include <cstdint>

namespace nameplate_lab {
constexpr unsigned MaxGlyphs = 36;
constexpr unsigned MaxDebuffs = 32;
constexpr unsigned MaxQuads = MaxGlyphs + 6 + 9 + MaxDebuffs;
struct DebuffRow {
    unsigned count=0;
    std::uint16_t effects[MaxDebuffs]{};
    float size=16; // Local name units, before the existing size/width transform.
};
struct DebuffBounds {float left=0,right=0,top=0;bool occupied=false;};
struct TraitLabel {
    std::uint16_t bits=0;
    float scale=1;
};
struct LevelLabel {
    unsigned length=0;
    char text[7]{};
    std::uint32_t color=0;
    float scale=1; // User factor relative to the existing name size.
};
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
    Quad quads[MaxQuads];
};
struct StatusIcons {
    bool replace = false; // Only player names use independent status icons.
    std::uint8_t active = 0; // Party, bazaar, linkshell, from outside toward the name.
    std::uint32_t linkshellColor = 0;
};
static_assert(sizeof(Glyph) == 44 && sizeof(Vertex) == 28 && sizeof(Quad) == 124);
// Shared by collection and layout so each needed glyph is read only once.
bool ExpandName(const Input& input, std::uint8_t (&codes)[MaxGlyphs], unsigned& count,
    unsigned& nameCount, const StatusIcons* icons = nullptr) noexcept;
// Pure layout: no pointers, graphics calls, allocations, or changes to game state.
bool Build(const Input& input, Output& output, const StatusIcons* icons = nullptr,
    const LevelLabel* level = nullptr, const TraitLabel* traits = nullptr,
    const DebuffRow* debuffs = nullptr, DebuffBounds* bounds = nullptr) noexcept;
}
