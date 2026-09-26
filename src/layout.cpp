#include "layout.h"
#include <cmath>

namespace nameplate_lab {
namespace {
bool finite(float value) noexcept { return std::isfinite(value); }
float rounded(double value) noexcept { return static_cast<float>(value); }
}

bool Build(const Input& in, Output& out) noexcept {
    out.count = 0;
    if (in.length > MaxGlyphs || !finite(in.x) || !finite(in.y) || !finite(in.z)
        || !finite(in.scaleX) || !finite(in.scaleY) || in.scaleX <= 0 || in.scaleY <= 0
        || in.scaleX > 128 || in.scaleY > 128 || in.z < 0 || in.z >= 1) return false;
    std::uint8_t codes[MaxGlyphs]{};
    unsigned count = 0;
    const auto append = [&](std::uint8_t code) {
        if (count == MaxGlyphs) return false;
        codes[count++] = code;
        return true;
    };
    for (unsigned i = 0; i < in.length; ++i) {
        const auto code = in.text[i];
        if (code >= 0xC8 && code <= 0xCD) {
            const unsigned n = code - 0xC8;
            if (!append(in.expansionBase[n])) return false;
            for (unsigned j = 0; j < in.expansionCount[n]; ++j)
                if (!append(0xAA)) return false;
        } else {
            if (!append(code)) return false;
            if (code == 0xAC && !append(0xAD)) return false;
        }
    }
    struct Size { int width, height; } sizes[MaxGlyphs]{};
    int total = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto code = codes[i];
        if (code == 10) continue;
        if (code < 32) return false;
        const auto& g = in.glyphs[code];
        if (!g.valid || g.width < 0 || g.width > 256 || g.height < 0 || g.height > 256
            || g.textureGroup > 1 || g.offsetX < -256 || g.offsetX > 256
            || g.offsetY < -256 || g.offsetY > 256) return false;
        for (float uv : g.uv) if (!finite(uv)) return false;
        auto& size = sizes[i];
        size = {g.width, g.height};
        if (i && code >= 0x8E && code <= 0xA8) {
            total += size.width / 2;
            size.width = static_cast<int>(static_cast<double>(size.width) * 0.8f);
            size.height = static_cast<int>(static_cast<double>(size.height) * 0.8f);
        } else if (i && code == 0xAA) {
            size.width /= 2;
            size.height /= 2;
            total = static_cast<int>(total + size.width * 0.5);
        } else if (code != 0xAD) total += size.width;
    }
    const float start = rounded(-total * 0.5);
    float pen = start, line = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto code = codes[i];
        if (code == 10) { pen = start; line += 8; continue; }
        const auto& g = in.glyphs[code];
        const auto size = sizes[i];
        double scale = 1, advance = size.width;
        int dx = 0, dy = 0;
        if (i && code >= 0x8E && code <= 0xA8) {
            scale = 0.8f; advance = size.width * 0.625;
            dx = -(size.width / 2); dy = size.height / 2;
        } else if (i && code == 0xAA) {
            advance = size.width * 0.5; dx = -size.width; dy = size.height;
        } else if (code == 0xAD) {
            scale = 0.5; advance = 0; dx = -2-size.width; dy = -2;
        }
        const double leftExact = static_cast<double>(g.offsetX + dx) + pen;
        const float left = rounded(leftExact);
        const float right = rounded(leftExact + size.width * scale);
        const float top = rounded(static_cast<double>(g.offsetY + dy) + line);
        const float bottom = rounded(static_cast<double>(rounded(size.height * scale)) + top);
        pen = rounded(advance + pen);
        auto& q = out.quads[out.count++];
        q.code = code; q.textureGroup = g.textureGroup;
        auto color = g.textureGroup == 1 ? in.shellColor : in.nameColor;
        q.alphaReference = color == in.shellColor ? 0x60u : 0u;
        if (q.alphaReference) {
            const auto rgb = code == 0x92 ? color : 0x80808080u;
            color = 0x80000000u | ((rgb & 255u) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 255u);
        }
        for (unsigned v = 0; v < 4; ++v) {
            const float localX = v & 1 ? right : left;
            const float localY = v & 2 ? bottom : top;
            q.vertices[v] = {
                rounded(static_cast<double>(in.x) + rounded(static_cast<double>(in.scaleX) * localX)),
                rounded(static_cast<double>(in.y) + rounded(static_cast<double>(in.scaleY) * localY)),
                in.z, 1, color, g.uv[v*2], g.uv[v*2+1]
            };
        }
    }
    return true;
}
}
