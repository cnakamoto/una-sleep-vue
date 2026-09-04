#ifndef STAGETIMELINEBAR_HPP
#define STAGETIMELINEBAR_HPP

#include <touchgfx/widgets/Widget.hpp>
#include "Commands.hpp"

// Stage timeline of the last night as an arc band hugging the bottom
// edge of the round screen: x = time along the arc (bed at the
// lower-left end, wake at the lower-right), color = stage at that
// time. Same palette as tools/plot_night.py. Data arrives from the
// service 2-bit-packed (CustomMessage::SleepTimelineData).
//
// The band spans 45..135 degrees (screen coords, y down): the button
// legend icons sit at ~30 deg (R2) and ~160 deg (L2) at the same
// radius, so the arc must not reach further around the "lower half".
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

    // Arc geometry (absolute screen coords; the 240x240 display is a
    // circle of radius 120 centered at (120,120)).
    static constexpr int16_t kCenterX = 120;
    static constexpr int16_t kCenterY = 120;
    static constexpr int16_t kInnerR = 104;   // band inner radius
    static constexpr int16_t kOuterR = 118;   // band outer radius
    static constexpr float   kArcStartDeg = 45.0f;   // wake end (right)
    static constexpr float   kArcEndDeg = 135.0f;    // bed end (left)
    // columns per degree of arc
    static constexpr float kColsPerDeg =
        static_cast<float>(kMaxColumns) / (kArcEndDeg - kArcStartDeg);

    static touchgfx::colortype stageColor(uint8_t stage);

    uint8_t mColumns[kMaxColumns / 4];
    bool    mHasData;
};

#endif // STAGETIMELINEBAR_HPP
