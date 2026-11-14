#pragma once

#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace pulse::ui {

// All coordinates are pixels in the same coordinate space. A long tip is
// constrained to the viewport; its caller must fit the text to that width.
inline RECT ClampDragFeedbackRect(RECT rect, const RECT& viewport) {
    const LONG width = std::clamp(rect.right - rect.left, 0L,
                                  std::max(0L, viewport.right - viewport.left));
    const LONG height = std::clamp(rect.bottom - rect.top, 0L,
                                   std::max(0L, viewport.bottom - viewport.top));
    const LONG x = std::clamp(rect.left, viewport.left, std::max(viewport.left, viewport.right - width));
    const LONG y = std::clamp(rect.top, viewport.top, std::max(viewport.top, viewport.bottom - height));
    return {x, y, x + width, y + height};
}

inline RECT PlaceDragDropTip(POINT preferred, SIZE size, const RECT& viewport,
                              const RECT* drag_visual, LONG gap) {
    size.cx = std::clamp(size.cx, 0L, std::max(0L, viewport.right - viewport.left));
    size.cy = std::clamp(size.cy, 0L, std::max(0L, viewport.bottom - viewport.top));
    const auto place = [&](POINT point) {
        return ClampDragFeedbackRect({point.x, point.y, point.x + size.cx, point.y + size.cy}, viewport);
    };
    if (!drag_visual) return place(preferred);

    const RECT avoid{drag_visual->left - gap, drag_visual->top - gap,
                     drag_visual->right + gap, drag_visual->bottom + gap};
    // Prefer the ordinary cursor offset, then a separate row below the cards.
    // Above/left alternatives keep both layers readable at screen edges.
    const std::array<POINT, 5> candidates{{preferred,
        {preferred.x, avoid.bottom}, {preferred.x, avoid.top - size.cy},
        {avoid.right, preferred.y}, {avoid.left - size.cx, preferred.y}}};
    RECT best = place(preferred);
    auto least_overlap = (std::numeric_limits<std::int64_t>::max)();
    for (const POINT point : candidates) {
        const RECT candidate = place(point);
        const std::int64_t overlap =
            static_cast<std::int64_t>(std::max(0L, std::min(candidate.right, avoid.right) -
                                                  std::max(candidate.left, avoid.left))) *
            std::max(0L, std::min(candidate.bottom, avoid.bottom) -
                         std::max(candidate.top, avoid.top));
        if (overlap < least_overlap) {
            best = candidate;
            least_overlap = overlap;
        }
        if (overlap == 0) break;
    }
    return best;
}

} // namespace pulse::ui
