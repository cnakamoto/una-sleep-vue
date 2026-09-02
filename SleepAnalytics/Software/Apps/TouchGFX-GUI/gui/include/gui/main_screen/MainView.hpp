#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>

#include <touchgfx/Unicode.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
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

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    // Main readout: replaces the Designer's single-use "Hello World" text
    // (textArea1 is removed in setupScreen — no Designer round-trip needed).
    // This TouchGFX port stores a pointer to the wildcard, not a copy —
    // the buffer must outlive the widget (it's a member, so it does).
    static constexpr uint16_t kTextBufferSize = 224;
    touchgfx::TextAreaWithOneWildcard mainText;
    touchgfx::Unicode::UnicodeChar textBuffer[kTextBufferSize];

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
};

#endif // MAINVIEW_HPP
