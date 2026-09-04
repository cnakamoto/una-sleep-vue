#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>

#include <touchgfx/Unicode.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <gui/containers/StageTimelineBar.hpp>
#include "Commands.hpp"

class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /** Tracking state arrived (state change or periodic refresh). */
    void onSessionState(const CustomMessage::SessionStateData& state);

    /** Last completed night's summary arrived. */
    void onSleepSummary(const CustomMessage::SleepSummaryData& summary);

    /** One history row arrived (streamed after a page switch). */
    void onHistoryEntry(const CustomMessage::HistoryEntryData& entry);

    /** One stage-timeline chunk arrived (streamed after the summary). */
    void onSleepTimeline(const CustomMessage::SleepTimelineData& timeline);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    // Main readout: replaces the Designer's single-use "Hello World" text
    // (textArea1 is removed in setupScreen — no Designer round-trip needed).
    // This TouchGFX port stores a pointer to the wildcard, not a copy —
    // the buffer must outlive the widget (it's a member, so it does).
    //
    // NOTE: this port's Unicode::snprintf reads %s arguments as wide
    // UnicodeChar* strings — passing char* makes it scan past the byte
    // null through stack memory (v0.7.1 garbled-text bug). All text is
    // therefore composed with C snprintf into stagingBuf and widened
    // once via Unicode::strncpy (which does take char*).
    static constexpr uint16_t kTextBufferSize = 224;
    touchgfx::TextAreaWithOneWildcard mainText;
    touchgfx::Unicode::UnicodeChar textBuffer[kTextBufferSize];
    char stagingBuf[kTextBufferSize];

    // Status field: always-visible app state string, "IDLE" or "SLEEP".
    static constexpr uint16_t kStatusBufferSize = 8;
    touchgfx::TextAreaWithOneWildcard statusText;
    touchgfx::Unicode::UnicodeChar statusBuffer[kStatusBufferSize];

    // Stage bar: per-column stage timeline of the last completed night
    // (x = time, bed -> wake; colors follow tools/plot_night.py).
    StageTimelineBar stageBar;

    // Timeline chunk accumulation (kChunks chunks of 40 columns, 2-bit
    // packed). Bar draws only when a complete set matches the summary.
    uint8_t  mTimelineCols[CustomMessage::SleepTimelineData::kMaxColumns / 4];
    uint32_t mTimelineDateKey = 0;
    uint8_t  mTimelineMask = 0;
    bool     mTimelineValid = false;

    CustomMessage::SessionStateData mState { };
    CustomMessage::SleepSummaryData mSummary { };
    bool mHasState = false;
    bool mHasSummary = false;

    // L1 pages the IDLE view between last-night detail and week history.
    enum class Page : uint8_t { SUMMARY, HISTORY };
    Page mPage = Page::SUMMARY;
    CustomMessage::HistoryEntryData mRows[CustomMessage::HistoryEntryData::kMaxRows];
    uint8_t mRowCount = 0;

    void render();
    void renderHistory();
    void renderStatus(bool tracking);
    void renderStageBar();
    void hideStageBar();
    void flushMainText();  // widen stagingBuf into textBuffer + redraw
};

#endif // MAINVIEW_HPP
