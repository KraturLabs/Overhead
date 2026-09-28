#include "health.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace nameplate_lab {
namespace {
void dim(Quad& q) noexcept {
    for (auto& v : q.vertices) {
        const auto rgb = v.color;
        const auto brightest = std::max({rgb & 255u, (rgb >> 8) & 255u, (rgb >> 16) & 255u});
        const auto gray = brightest * 3u / 8u;
        v.color = (rgb & 0xFF000000u) | gray * 0x010101u;
    }
}
}

HealthFill MeasureHealth(const Output& output, unsigned percent) noexcept {
    if (percent >= 100 || output.count > MaxQuads) return {};
    float left = std::numeric_limits<float>::max(), right = -left;
    for (unsigned i = 0; i < output.count; ++i) {
        const auto& q = output.quads[i];
        if (!HealthLetter(q)) continue;
        const float x0 = q.vertices[0].x, x1 = q.vertices[1].x;
        if (!std::isfinite(x0) || !std::isfinite(x1) || x1 < x0) return {};
        left = std::min(left, x0); right = std::max(right, x1);
    }
    if (!(right > left)) return {};
    return {static_cast<float>(left + (static_cast<double>(right) - left) * percent / 100.0), true};
}

unsigned HealthQuads(const Quad& original, const HealthFill& fill, Quad (&pieces)[2]) noexcept {
    pieces[0] = original;
    if (!fill.enabled || !HealthLetter(original) || !std::isfinite(fill.boundary)) return 1;
    const float left = original.vertices[0].x, right = original.vertices[1].x;
    if (fill.tint)
        for (auto& v : pieces[0].vertices) v.color = (v.color & 0xFF000000u) | fill.tint;
    if (right <= fill.boundary) return 1;
    if (left >= fill.boundary) { dim(pieces[0]); return 1; }
    // The boundary is strictly inside this glyph. Preserve each edge's texture
    // coordinates rather than stretching either half of its letter artwork.
    pieces[1] = pieces[0];
    const double fraction = (static_cast<double>(fill.boundary) - left) / (static_cast<double>(right) - left);
    for (unsigned row : {0u, 2u}) {
        // Edges come from the tinted copy so a split letter keeps one color.
        const auto a = pieces[0].vertices[row];
        const auto b = pieces[0].vertices[row + 1];
        auto edge = a;
        edge.x = fill.boundary;
        edge.u = static_cast<float>(a.u + (static_cast<double>(b.u) - a.u) * fraction);
        edge.v = static_cast<float>(a.v + (static_cast<double>(b.v) - a.v) * fraction);
        pieces[0].vertices[row + 1] = edge;
        pieces[1].vertices[row] = edge;
    }
    dim(pieces[1]);
    return 2;
}
}
