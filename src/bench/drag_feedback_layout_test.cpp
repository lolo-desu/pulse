#include "../ui/drag_feedback_layout.h"

#include <cstdio>

namespace {
int failures = 0;

void Check(bool okay, const char* label) {
    std::printf("[%s] %s\n", okay ? "PASS" : "FAIL", label);
    if (!okay) ++failures;
}

bool Inside(const RECT& rect, const RECT& viewport) {
    return rect.left >= viewport.left && rect.top >= viewport.top &&
           rect.right <= viewport.right && rect.bottom <= viewport.bottom;
}

bool Separate(const RECT& rect, RECT visual, LONG gap) {
    visual.left -= gap;
    visual.top -= gap;
    visual.right += gap;
    visual.bottom += gap;
    return rect.right <= visual.left || rect.left >= visual.right ||
           rect.bottom <= visual.top || rect.top >= visual.bottom;
}
} // namespace

int main() {
    using pulse::ui::ClampDragFeedbackRect;
    using pulse::ui::PlaceDragDropTip;
    const RECT viewport{8, 8, 532, 232};
    const RECT screenshot_card{244, 92, 364, 130};
    const RECT screenshot_old_tip{242, 94, 462, 118};
    Check(!Separate(screenshot_old_tip, screenshot_card, 0),
          "screenshot regression: cursor offsets used to overlap the filename card");
    const RECT screenshot_tip = PlaceDragDropTip({242, 94}, {220, 24}, viewport, &screenshot_card, 8);
    Check(Inside(screenshot_tip, viewport) && Separate(screenshot_tip, screenshot_card, 8) &&
          screenshot_tip.top >= screenshot_card.bottom + 8,
          "action tip occupies its own row below the filename and shadow");

    const RECT bottom_card{378, 170, 528, 228};
    const RECT bottom_tip = PlaceDragDropTip({390, 188}, {220, 24}, viewport, &bottom_card, 8);
    Check(Inside(bottom_tip, viewport) && Separate(bottom_tip, bottom_card, 8) &&
          bottom_tip.bottom <= bottom_card.top - 8,
          "bottom-right window edge flips the complete tip above the cards");

    const RECT upper_card{8, 8, 300, 80};
    const RECT upper_tip = PlaceDragDropTip({-20, -20}, {220, 24}, viewport, &upper_card, 8);
    Check(Inside(upper_tip, viewport) && Separate(upper_tip, upper_card, 8),
          "top-left window edge never produces negative or obscured tip coordinates");

    const RECT narrow{20, 20, 200, 180};
    const RECT long_tip = PlaceDragDropTip({-30, 200}, {800, 48}, narrow, nullptr, 8);
    Check(Inside(long_tip, narrow) && long_tip.right - long_tip.left == 180 &&
          long_tip.bottom - long_tip.top == 48,
          "a wrapped long operation label fits the visible window width and height");

    const RECT lagging_fan{20, 35, 285, 115};
    const RECT free_tip = PlaceDragDropTip({340, 145}, {180, 24}, viewport, &lagging_fan, 8);
    Check(free_tip.left == 340 && free_tip.top == 145,
          "spring animation uses actual card bounds rather than a fixed cursor reservation");

    const RECT shifted_view{-1912, -392, -1388, -168};
    const RECT shifted_card{-1676, -308, -1556, -270};
    const RECT shifted_tip = PlaceDragDropTip({-1678, -306}, {220, 24}, shifted_view, &shifted_card, 8);
    Check(shifted_tip.left == screenshot_tip.left - 1920 && shifted_tip.top == screenshot_tip.top - 400 &&
          Inside(shifted_tip, shifted_view) && Separate(shifted_tip, shifted_card, 8),
          "screen/client conversion is translation invariant on negative-coordinate monitors");

    bool grid_ok = true;
    int cases = 0;
    for (LONG scale = 1; scale <= 3; ++scale) {
        const RECT work_area{-540 * scale, -100 * scale, 0, 200 * scale};
        for (LONG x = -580; x <= 40; x += 31) {
            for (LONG y = -140; y <= 240; y += 19) {
                const RECT wanted{x * scale, y * scale, (x + 250) * scale, (y + 70) * scale};
                const RECT fan = ClampDragFeedbackRect(wanted, work_area);
                const RECT tip = PlaceDragDropTip({(x + 14) * scale, (y + 16) * scale},
                    {220 * scale, 24 * scale}, work_area, &fan, 8 * scale);
                grid_ok = grid_ok && Inside(fan, work_area) && Inside(tip, work_area) &&
                          Separate(tip, fan, 8 * scale);
                ++cases;
            }
        }
    }
    Check(grid_ok, "100/200/300 percent DPI: all monitor edges keep card stack and tip visible and separate");
    std::printf("%d edge/DPI combinations checked\n", cases);
    return failures ? 1 : 0;
}
