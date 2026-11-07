#include "../ui/toolbar_layout.h"
#include <cstdio>
#include <vector>

namespace {
bool Visible(const D2D1_RECT_F& r) { return r.right > r.left && r.bottom > r.top; }
bool Contains(const D2D1_RECT_F& r, float x, float y) {
    return Visible(r) && x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
bool Check(float width, float scale, float filter, bool drop) {
    const float top = 32 * scale;
    const auto l = pulse::ui::MakeClassicRibbonLayout(width, scale, top, 42, 9, drop, filter);
    std::vector<D2D1_RECT_F> rects;
    for (const auto& r : l.ribbon_tabs) rects.push_back(r);
    for (const auto& r : l.navigation) rects.push_back(r);
    for (const auto& r : l.pins) rects.push_back(r);
    for (const auto& r : {l.address, l.search, l.create, l.sort, l.filter, l.group, l.overflow}) rects.push_back(r);
    for (size_t i = 0; i < rects.size(); ++i) {
        const auto& r = rects[i];
        if (r.right < r.left || r.bottom < r.top) return false;
        if (!Visible(r)) continue;
        if (r.left < 0 || r.right > width + 0.01f || r.top < top || r.bottom > top + 156 * scale) return false;
        for (size_t j = i + 1; j < rects.size(); ++j) {
            const auto& b = rects[j];
            if (Visible(b) && std::min(r.right, b.right) > std::max(r.left, b.left) + 0.01f &&
                std::min(r.bottom, b.bottom) > std::max(r.top, b.top) + 0.01f) return false;
        }
    }
    if (!Visible(l.address) || !Visible(l.search)) return false;
    if (filter > 0 && l.filter.right - l.filter.left < 100 * scale) return false;
    for (const auto& nav : l.navigation) {
        const float x = (nav.left + nav.right) / 2, y = (nav.top + nav.bottom) / 2;
        if (!Contains(nav, x, y) || Contains(l.address, x, y) || Contains(l.search, x, y)) return false;
    }
    return true;
}
}
int main() {
    int failures = 0, cases = 0;
    for (const float width : {320.0f, 640.0f, 1128.0f})
        for (const float scale : {1.0f, 1.5f, 2.0f})
            for (const bool physical : {false, true})
                for (const float filter : {0.0f, 0.5f, 1.0f})
                    for (const bool drop : {false, true}) {
                        ++cases;
                        const float pixels = physical ? width : width * scale;
                        if (!Check(pixels, scale, filter, drop)) {
                            ++failures;
                            std::printf("[FAIL] width=%.0f scale=%.1f filter=%.1f drop=%d\n", pixels, scale, filter, drop);
                        }
                    }
    std::printf("[%s] classic ribbon geometry: %d cases, %d failures\n", failures ? "FAIL" : "PASS", cases, failures);
    return failures ? 1 : 0;
}
