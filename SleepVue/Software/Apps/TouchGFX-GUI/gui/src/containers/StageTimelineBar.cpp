#include <gui/containers/StageTimelineBar.hpp>

#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/lcd/LCD.hpp>
#include <touchgfx/Color.hpp>

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
    // The framework clips `area` to the widget rect; with the widget
    // sized 1 px per column, local x is the column index. Draw
    // same-stage runs with one fillRect each (Box::draw pattern).
    const touchgfx::Rect abs = getAbsoluteRect();
    const int16_t end = area.x + area.width;
    int16_t x = area.x;
    while (x < end) {
        const uint8_t st = (mColumns[x >> 2] >> ((x & 3) * 2)) & 0x3;
        int16_t runEnd = x + 1;
        while (runEnd < end
               && ((mColumns[runEnd >> 2] >> ((runEnd & 3) * 2)) & 0x3) == st) {
            ++runEnd;
        }
        touchgfx::Rect r(abs.x + x, abs.y + area.y, runEnd - x, area.height);
        touchgfx::HAL::lcd().fillRect(r, stageColor(st), 255);
        x = runEnd;
    }
}

touchgfx::Rect StageTimelineBar::getSolidRect() const
{
    if (mHasData) {
        return touchgfx::Rect(0, 0, getWidth(), getHeight());
    }
    return touchgfx::Rect();
}
