#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace text_font {
constexpr unsigned First=32, Last=126, Characters=Last-First+1;
constexpr unsigned Sheet=512, TextureWidth=Sheet*2, TextureHeight=Sheet;
// Settings UI only: installed scalable font families, as UTF-8 names.
std::vector<std::string> InstalledFamilies();
struct Glyph {
    int advance=0, left=0, top=0, width=0, height=0;
    unsigned x=0,y=0;
};
struct Font {
    Glyph glyphs[Characters]{};
    short kerning[Characters][Characters]{};
    float unit=1; // Raster pixels -> native name units, based on actual capital height.
    float baseline=0; // GDI baseline below the native name's top anchor.
    unsigned outline=3,substitutions=0;
    unsigned spill=0; // Softening blur's reach beyond the outline, in raster pixels.
    unsigned Pad() const {return outline+spill;} // Glyph cell padding around the letter bitmap.
    bool substituted[Characters]{};
    wchar_t face[32]{};
    char error[128]{};
    std::vector<std::uint32_t> pixels; // Setup/upload only; released after upload.
    // An installed family uses bold weight; otherwise an empty path selects Tahoma Bold.
    // File resources are private and
    // released after preparing the atlas; no installed-font changes or live lookups.
    // Soften 1-2 blurs coverage slightly so the game's resampling steps slanted edges less.
    bool Prepare(const wchar_t* path=L"",unsigned border=3,const wchar_t* family=L"",bool italic=false,unsigned soften=0);
};
}
