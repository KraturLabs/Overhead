#pragma once
#include "layout.h"

namespace nameplate_lab {
struct Options {
    float scale = 1;
    float width = 1;
    bool correctAspect = false; // Legacy saved settings only; presets use Width.
    unsigned filter = 2; // 0 native, 1 sharp/nearest, 2 smooth/linear.
    bool keepCursor = false; // Opt-in native arrow when the target panel is hidden.
    bool showStatusIcons = true; // Independent party / bazaar / linkshell icons, detached left.
    bool showLevels = true, autoCheck = true;
    bool levelsTargetOnly = false;
    float levelScale = 1;
    bool showTraits = true;
    bool traitsTargetOnly = false;
    float traitScale = 1;
    bool showDebuffs = true, debuffsTargetOnly = false;
    bool selfDebuffs = true, partyDebuffs = true, allianceDebuffs = true;
    unsigned debuffSize = 16;
    float damageScale = 1, damageWidth = 1;
    bool damageEnabled = false, damageCorrectAspect = false;
    unsigned mode = 3;   // All supported names plus enemy HP, by default.
};
bool ParseFactor(const char* text, float& value) noexcept;
bool ValidOptions(const Options& value) noexcept;
// Settings are validated at input boundaries; drawing validates live scales and results.
bool SizeScale(float& scaleX,float& scaleY,const Options& options) noexcept;
bool SizeName(Input& input, const Options& options) noexcept;
bool OriginalWidth(unsigned screenWidth, unsigned screenHeight, float& width) noexcept;
bool ApplyOriginalSizing(Options& options, unsigned screenWidth, unsigned screenHeight) noexcept;
bool ApplyDamageSizing(Options& options, unsigned screenWidth, unsigned screenHeight) noexcept;
Options LoadOptions(const char* path) noexcept;
bool SaveOptions(const char* path, const Options& value) noexcept;
}
