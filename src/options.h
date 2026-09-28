#pragma once
#include "layout.h"

namespace nameplate_lab {
// Plate categories: a name uses the first row it matches. Each row holds the
// details shown for it; RowShow switches the whole row.
enum DetailRow : unsigned { RowTarget, RowSelf, RowParty, RowClaimedSelf, RowClaimedParty, RowClaimedOther, RowUnclaimed, RowCount };
enum DetailColumn : unsigned {
    RowShow=1, ShowHealth=2, ShowTp=4, ShowMp=8, ShowLevel=16, ShowTraits=32, ShowDebuffs=64, ShowAction=128, ShowDistance=256, ShowWeak=512, ShowResist=1024
};
constexpr unsigned EnemyColumns=RowShow|ShowHealth|ShowLevel|ShowTraits|ShowDebuffs|ShowAction|ShowDistance|ShowWeak|ShowResist;
// Columns that apply to each row; the rest are not applicable.
constexpr unsigned RowColumns[RowCount]={2047,RowShow|ShowHealth|ShowTp|ShowMp|ShowDebuffs|ShowAction,
    RowShow|ShowHealth|ShowTp|ShowMp|ShowDebuffs|ShowAction|ShowDistance,EnemyColumns,EnemyColumns,EnemyColumns,EnemyColumns};
// Existing details keep their earlier defaults; HP%/distance and weak/resist stay target-only.
constexpr unsigned DefaultRows[RowCount]={2047,RowShow|ShowHealth|ShowTp|ShowDebuffs|ShowAction,
    RowShow|ShowHealth|ShowTp|ShowDebuffs|ShowAction,RowShow|ShowLevel|ShowTraits|ShowDebuffs|ShowAction,
    RowShow|ShowLevel|ShowTraits|ShowDebuffs|ShowAction,RowShow|ShowLevel|ShowTraits|ShowDebuffs|ShowAction,
    RowShow|ShowLevel|ShowTraits|ShowDebuffs|ShowAction};
// Actions stay on top wherever a row can show them.
constexpr unsigned DefaultFront[RowCount]={ShowAction,ShowAction,ShowAction,ShowAction,ShowAction,ShowAction,ShowAction};
struct Options {
    float scale = 1;
    float width = 1;
    bool correctAspect = false; // Legacy saved settings only; presets use Width.
    unsigned filter = 2; // 0 native, 1 sharp/nearest, 2 smooth/linear.
    bool keepCursor = false; // Opt-in native arrow when the target panel is hidden.
    bool hideTarget = false; // Hide the game's target window ourselves; HideParty optional.
    bool showStatusIcons = true; // Native player-name icon prefix, detached left of the name.
    bool autoCheck = true;
    unsigned debuffSize = 16;
    unsigned rows[RowCount] = {DefaultRows[0],DefaultRows[1],DefaultRows[2],DefaultRows[3],DefaultRows[4],DefaultRows[5],DefaultRows[6]};
    unsigned front[RowCount] = {DefaultFront[0],DefaultFront[1],DefaultFront[2],DefaultFront[3],DefaultFront[4],DefaultFront[5],DefaultFront[6]};
    bool friendlyHealth = false; // Deplete your own and party/alliance names by HP.
    bool npcFeatures = false; // NPCs keep the native plate unless on.
    bool unclaimedDamagedOnly = false; // Unclaimed row only once the monster is damaged.
    float actionScale = .6f; // Action size.
    float traitScale = .8f; // Trait icons, relative to the name height.
    float weakScale = 1, resistScale = 1; // Weakness/resistance icons, relative to their fixed row height.
    bool scrollXp = false; // Experimental: gained points drift from your name.
    bool growTarget = false; // Experimental: control the selected target plate's distant size.
    float growFarSize = 1; // Readability size at 25 yalms, relative to native full size.
    float damageScale = 1, damageWidth = 1;
    bool damageEnabled = false, damageCorrectAspect = false;
    unsigned mode = 3;   // All supported names plus enemy HP, by default.
};
bool ParseFactor(const char* text, float& value) noexcept;
bool ValidOptions(const Options& value) noexcept;
// Settings are validated at input boundaries; drawing validates live scales and results.
bool SizeScale(float& scaleX,float& scaleY,const Options& options) noexcept;
bool SizeName(Input& input, const Options& options) noexcept;
// Fade added readability from 25 yalms to zero at 3; never shrink native size.
// smoothedNative: the native factor low-passed over time (0 = use nativeFactor).
float GrowFactor(float nativeFactor, float distance, float farSize, float smoothedNative = 0) noexcept;
bool GrowName(Input& input, float factor) noexcept;
bool OriginalWidth(unsigned screenWidth, unsigned screenHeight, float& width) noexcept;
bool ApplyOriginalSizing(Options& options, unsigned screenWidth, unsigned screenHeight) noexcept;
bool ApplyDamageSizing(Options& options, unsigned screenWidth, unsigned screenHeight) noexcept;
Options LoadOptions(const char* path) noexcept;
bool SaveOptions(const char* path, const Options& value) noexcept;
}
