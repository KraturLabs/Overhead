#pragma once
#include <cstdint>

namespace nameplate_lab {
constexpr unsigned MaxGlyphs = 36;
constexpr unsigned MaxDebuffs = 32;
constexpr unsigned MaxLabel = 31; // Action names; HP% and distance are shorter.
constexpr unsigned MaxFloating = 3, MaxFloatText = 20; // Gained points below your name.
// Name, level, traits, debuffs, then HP%, MP, TP (4 each), distance (5), an action name
// and the floating point gains.
constexpr unsigned MaxQuads = MaxGlyphs + 6 + 3 + 9 + MaxDebuffs + 4 + 4 + 4 + 5 + MaxLabel + MaxFloating*MaxFloatText;
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
    char check[4]{}; // Check rank abbreviation drawn above "Lv."; empty when unknown.
    unsigned checkLength=0;
    std::uint32_t color=0;
    float scale=1; // User factor relative to the existing name size.
};
// Native-font text beside or below the name. Missing glyphs omit only the label.
struct TextLabel {
    unsigned length=0;
    char text[MaxLabel+1]{};
    std::uint32_t color=0; // Half-intensity RGB under native doubled modulation.
};
// Centered below the name: drop in name heights, alpha 0-1 over the name's own.
struct FloatingLabel {
    TextLabel text;
    float drop=0, alpha=0;
};
struct SideLabels {
    TextLabel health, mp, tp, distance, action;
    FloatingLabel floating[MaxFloating];
    bool floatUp=false; // Floating labels rise from above the name instead.
    float scale=1, actionScale=.6f; // User factors relative to the name size.
    // Parts drawn over world geometry, as DetailColumn bits; RowShow is the name.
    unsigned front=0;
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
// Set in Quad::code for quads drawn without the depth test, over world geometry.
constexpr std::uint32_t FrontCode = 0x10000u;
struct Quad {
    std::uint32_t code, textureGroup, alphaReference;
    Vertex vertices[4];
};
struct Output {
    std::uint32_t count;
    Quad quads[MaxQuads];
};
// Player names: the native formatter's leading icon prefix (its own priority
// winner plus any stacked second-slot icons) is drawn left of the name, apart
// from it, so the name alone is centered.
struct StatusIcons {
    bool replace = false; // Only single-line player names separate the prefix.
    bool show = true;     // False omits the prefix entirely.
};
static_assert(sizeof(Glyph) == 44 && sizeof(Vertex) == 28 && sizeof(Quad) == 124);
// Shared by collection and layout so each needed glyph is read only once.
bool ExpandName(const Input& input, std::uint8_t (&codes)[MaxGlyphs], unsigned& count,
    unsigned& nameCount, const StatusIcons* icons = nullptr) noexcept;
// Pure layout: no pointers, graphics calls, allocations, or changes to game state.
bool Build(const Input& input, Output& output, const StatusIcons* icons = nullptr,
    const LevelLabel* level = nullptr, const TraitLabel* traits = nullptr,
    const DebuffRow* debuffs = nullptr, DebuffBounds* bounds = nullptr,
    const SideLabels* labels = nullptr) noexcept;
}
