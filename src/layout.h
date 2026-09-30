#pragma once
#include <cstdint>
namespace text_font {struct Font;}

namespace overhead {
constexpr unsigned MaxGlyphs = 36;
constexpr unsigned MaxDebuffs = 32;
constexpr unsigned TraitTextureHeight = 128; // Trait/modifier atlas: 32 px cells, 8 per row.
constexpr unsigned MaxLabel = 31; // Action names; HP% and distance are shorter.
constexpr unsigned MaxFloating = 3, MaxFloatText = 20; // Gained points below your name.
// Name, pinned linkshell/bazaar, level, traits, debuffs, then HP%, MP, TP (4 each), distance (5), weakness and
// resistance rows (two bar quads and up to 12 icons each), an action name and the floating point gains.
constexpr unsigned MaxQuads = MaxGlyphs + 2 + 6 + 3 + 9 + MaxDebuffs + 4 + 4 + 4 + 5 + 2*(2+12) + MaxLabel + MaxFloating*MaxFloatText;
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
    bool reserve=false; // Hold "Lv.00" room while the level is still unknown so details do not shift.
};
// Text beside or below the name. Missing glyphs omit only the label.
struct TextLabel {
    unsigned length=0;
    char text[MaxLabel+1]{};
    std::uint32_t color=0; // Half-intensity RGB under native doubled modulation.
};
// Centered below the name: drop in name heights, alpha 0-1 over the name's own.
struct FloatingLabel {
    TextLabel text;
    float drop=0, alpha=0;
    unsigned superStart=0, superLength=0; // Run drawn small and lowered (the chain count after "Chain").
    unsigned unitStart=MaxLabel+1; // Trailing unit ("XP") drawn at half size and raised.
};
struct SideLabels {
    TextLabel health, mp, tp, distance, action;
    FloatingLabel floating[MaxFloating];
    float scale=1, actionScale=.6f; // User factors relative to the name size.
// Damage modifier bits (traits.h); each row's icons scale within its fixed slot.
    std::uint16_t weak=0, resist=0;
    float weakScale=1, resistScale=1, traitScale=1;
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
    const text_font::Font* font=nullptr; // Prepared text; native resources remain for symbols.
    // Icon width relative to letter width (screen correction / Width); 0 means 1.
    // Icons keep 4:3 proportions whatever Width does to the letters.
    float iconRatio=0;
};
struct Vertex {
    float x, y, z, rhw;
    std::uint32_t color;
    float u, v;
};
// Set in Quad::code for quads drawn without the depth test, over world geometry.
constexpr std::uint32_t FrontCode = 0x10000u;
// Set in Quad::code for quads drawn before everything else, under the name.
constexpr std::uint32_t UnderCode = 0x20000u;
// Set in Quad::code for detail text drawn after all other detail text and outlines.
constexpr std::uint32_t TopCode = 0x40000u;
constexpr unsigned TextTexture=4;
struct Quad {
    std::uint32_t code, textureGroup, alphaReference;
    Vertex vertices[4];
};
struct Output {
    std::uint32_t count;
    Quad quads[MaxQuads];
    float textInset=0; // Outline padding in name screen units; excluded from HP bounds.
};
// Player names: the native formatter's leading icon prefix (its own priority
// winner plus any stacked second-slot icons) is drawn left of the name, apart
// from it, so the name alone is centered.
struct StatusIcons {
    bool replace = false; // Only single-line player names separate the prefix.
    bool show = true;     // False omits the prefix entirely.
    // Pinned outside the priority prefix, read from entity flags: linkshell over the
    // name's top-left corner, bazaar over its bottom-left corner.
    bool linkshell = false, bazaar = false;
    std::uint32_t linkshellColor = 0;
    bool pin = true;       // False leaves them in the native prefix like any other icon.
    bool pinOnTop = false; // Default tucks them under the name.
    float linkshellX = 0, linkshellY = 0, bazaarX = 0, bazaarY = 0; // User nudges, local name units.
};
constexpr std::uint8_t LinkshellGlyph = 0x92, BazaarGlyph = 0x9C;
static_assert(sizeof(Glyph) == 44 && sizeof(Vertex) == 28 && sizeof(Quad) == 124);
// Shared by collection and layout so each needed glyph is read only once.
bool ExpandName(const Input& input, std::uint8_t (&codes)[MaxGlyphs], unsigned& count,
    unsigned& nameCount, const StatusIcons* icons = nullptr) noexcept;
// Pure layout: no native memory reads, graphics calls, allocations, or game-state changes.
bool Build(const Input& input, Output& output, const StatusIcons* icons = nullptr,
    const LevelLabel* level = nullptr, const TraitLabel* traits = nullptr,
    const DebuffRow* debuffs = nullptr, DebuffBounds* bounds = nullptr,
    const SideLabels* labels = nullptr) noexcept;
}
