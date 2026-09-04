#include <gui/containers/StageTimelineBar.hpp>

#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/lcd/LCD.hpp>
#include <touchgfx/Color.hpp>

#include <cmath>
#include <cstring>

StageTimelineBar::StageTimelineBar()
    : mColumns{}
    , mHasData(false)
{
}

void StageTimelineBar::setColumns(const uint8_t* packed)
{
    memcpy(mColumns, packed, sizeof(mColumns));
    mHasData = true;
}

touchgfx::colortype StageTimelineBar::stageColor(uint8_t stage)
{
    // Sleep::Stage values (AWAKE 0 / LIGHT 1 / DEEP 2); palette follows
    // tools/plot_night.py.
    switch (stage) {
        case 2:  return touchgfx::Color::getColorFromRGB(63, 81, 181);
        case 1:  return touchgfx::Color::getColorFromRGB(66, 165, 245);
        default: return touchgfx::Color::getColorFromRGB(255, 167, 38);
    }
}

void StageTimelineBar::draw(const touchgfx::Rect& area) const
{
    if (!mHasData) {
        return;
    }
    // Paint the annulus sector pixel-exactly: a pixel belongs to the
    // band if its radius is within [kInnerR, kOuterR] and its angle
    // within [kArcStartDeg, kArcEndDeg]; the angle picks the column.
    // Same-stage runs on each row are merged into one fillRect.
    const touchgfx::Rect abs = getAbsoluteRect();
    const int16_t yEnd = area.y + area.height;
    const int16_t xBeg = area.x;
    const int16_t xEnd = area.x + area.width;

    for (int16_t ly = area.y; ly < yEnd; ++ly) {
        const int16_t py = abs.y + ly;
        const int32_t dy = py - kCenterY;
        int16_t runStart = -1;
        uint8_t runStage = 0;
        for (int16_t lx = xBeg; lx < xEnd; ++lx) {
            const int16_t px = abs.x + lx;
            const int32_t dx = px - kCenterX;
            const int32_t r2 = dx * dx + dy * dy;
            uint8_t st = 0xFF; // outside the band: skip pixel
            if (r2 >= kInnerR * kInnerR && r2 <= kOuterR * kOuterR) {
                // y-down coords: 0 deg = 3 o'clock, 90 deg = 6 o'clock
                const float ang = atan2f(static_cast<float>(dy),
                                         static_cast<float>(dx))
                                  * 57.29578f;
                if (ang >= kArcStartDeg && ang <= kArcEndDeg) {
                    int col = static_cast<int>((ang - kArcStartDeg) * kColsPerDeg);
                    if (col >= static_cast<int>(kMaxColumns)) {
                        col = kMaxColumns - 1;
                    }
                    // bed (left, 135 deg) -> wake (right, 45 deg)
                    col = static_cast<int>(kMaxColumns) - 1 - col;
                    st = (mColumns[col >> 2] >> ((col & 3) * 2)) & 0x3;
                }
            }
            if (st != runStage) {
                if (runStart >= 0) {
                    touchgfx::Rect r(abs.x + runStart, py, lx - runStart, 1);
                    touchgfx::HAL::lcd().fillRect(r, stageColor(runStage), 255);
                }
                runStart = (st != 0xFF) ? lx : -1;
                runStage = st;
            }
        }
        if (runStart >= 0) {
            touchgfx::Rect r(abs.x + runStart, py, xEnd - runStart, 1);
            touchgfx::HAL::lcd().fillRect(r, stageColor(runStage), 255);
        }
    }
}

touchgfx::Rect StageTimelineBar::getSolidRect() const
{
    // The band covers only a small part of the widget rect; never
    // claim solidity (background must be drawn behind the gaps).
    return touchgfx::Rect();
}
