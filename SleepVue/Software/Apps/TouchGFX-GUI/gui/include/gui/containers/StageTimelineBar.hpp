#ifndef STAGETIMELINEBAR_HPP
#define STAGETIMELINEBAR_HPP

#include <touchgfx/widgets/Widget.hpp>
#include "Commands.hpp"

// Stage timeline of the last night: one pixel per column, x = time
// (bed -> wake), full-height color = stage at that time. Same size,
// position, and palette as the old proportional summary bar (and
// tools/plot_night.py). Data arrives from the service 2-bit-packed
// (CustomMessage::SleepTimelineData).
class StageTimelineBar : public touchgfx::Widget
{
public:
    StageTimelineBar();

    /** Copy a complete packed column set (kMaxColumns x 2 bits). */
    void setColumns(const uint8_t* packed);

    virtual void draw(const touchgfx::Rect& area) const;
    virtual touchgfx::Rect getSolidRect() const;

private:
    static constexpr uint16_t kMaxColumns =
        CustomMessage::SleepTimelineData::kMaxColumns;

    static touchgfx::colortype stageColor(uint8_t stage);

    uint8_t mColumns[kMaxColumns / 4];
    bool    mHasData;
};

#endif // STAGETIMELINEBAR_HPP
