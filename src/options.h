#pragma once
#include "layout.h"

namespace nameplate_lab {
struct Options {
    float scale = 1;
    float width = 1;
    bool correctAspect = true;
    unsigned filter = 2; // 0 native, 1 sharp/nearest, 2 smooth/linear.
    float damageScale = 1, damageWidth = 1;
    bool damageEnabled = false, damageCorrectAspect = false;
    unsigned mode = 3;   // All supported names plus enemy HP, by default.
};
bool ParseFactor(const char* text, float& value) noexcept;
bool ValidOptions(const Options& value) noexcept;
bool SizeName(Input& input, const Options& options) noexcept;
bool OriginalWidth(float nativeAspect, unsigned screenWidth, unsigned screenHeight, float& width) noexcept;
bool ApplyOriginalSizing(Options& options, float nativeAspect, unsigned screenWidth, unsigned screenHeight) noexcept;
bool ApplyDamageSizing(Options& options, float nativeAspect, unsigned screenWidth, unsigned screenHeight) noexcept;
Options LoadOptions(const char* path) noexcept;
bool SaveOptions(const char* path, const Options& value) noexcept;
}
