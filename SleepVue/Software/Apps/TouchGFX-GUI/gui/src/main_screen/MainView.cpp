#include <gui/main_screen/MainView.hpp>
#include <texts/TextKeysAndLanguages.hpp>

#include "SleepTypes.hpp"

#include <cstdio>
#include <cstring>

namespace
{

// 452 min -> "7H32M"
void fmtDur(uint32_t minutes, char* out, size_t outSize)
{
    snprintf(out, outSize, "%luH%02luM",
             static_cast<unsigned long>(minutes / 60),
             static_cast<unsigned long>(minutes % 60));
}

// minutes since midnight -> "23:12"
void fmtClock(uint16_t minutes, char* out, size_t outSize)
{
    snprintf(out, outSize, "%02u:%02u",
             static_cast<unsigned>(minutes / 60),
             static_cast<unsigned>(minutes % 60));
}

// 240x240 round screen: y coordinates keep content inside the visible
// chord and clear of the button legend icons at the side edges.
constexpr int16_t kScreenW    = 240;
constexpr int16_t kStatusY    = 10;
constexpr int16_t kStatusH    = 32;  // Poppins SemiBold 25 line height
constexpr int16_t kBarX       = 40;
constexpr int16_t kBarY       = 48;
constexpr int16_t kBarW       = 160;
constexpr int16_t kBarH       = 16;
constexpr int16_t kTextX      = 16;
constexpr int16_t kTextY      = 76;
constexpr int16_t kTextYHist  = 46;

// Status colors are distinct from the stage palette so the field can't
// be mistaken for a bar segment.
touchgfx::colortype colorIdle()  { return touchgfx::Color::getColorFromRGB(96, 220, 120); }
touchgfx::colortype colorSleep() { return touchgfx::Color::getColorFromRGB(190, 130, 255); }

} // namespace

MainView::MainView()
{
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    buttons.setL1(ButtonsSet::AMBER);  // summary <-> week history
    buttons.setL2(ButtonsSet::NONE);
    buttons.setR1(ButtonsSet::AMBER);  // start / stop sleep session
    buttons.setR2(ButtonsSet::WHITE);  // exit

    // Replace the Designer's single-use "Hello World" text with a wildcard
    // text area driven from code (no Designer round-trip needed).
    remove(textArea1);

    mainText.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_18_L));
    mainText.setXY(kTextX, kTextY);
    mainText.setWidth(208);
    mainText.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    mainText.setLinespacing(2);
    mainText.setWildcard1(textBuffer);
    add(mainText);

    // Status field: full-width centered, fixed box (text centers inside).
    statusText.setTypedText(touchgfx::TypedText(T_TMP_SEMIBOLD_25));
    statusText.setPosition(0, kStatusY, kScreenW, kStatusH);
    statusText.setWildcard1(statusBuffer);
    add(statusText);

    // Stage timeline bar: geometry is fixed; column data + visibility
    // are driven by renderStageBar()/hideStageBar() on every render.
    stageBar.setPosition(kBarX, kBarY, kBarW, kBarH);
    add(stageBar);

    render();
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::onSessionState(const CustomMessage::SessionStateData& state)
{
    mState = state;
    mHasState = true;
    render();
}

void MainView::onSleepSummary(const CustomMessage::SleepSummaryData& summary)
{
    mSummary = summary;
    mHasSummary = true;
    render();
}

void MainView::onHistoryEntry(const CustomMessage::HistoryEntryData& entry)
{
    if (entry.row < CustomMessage::HistoryEntryData::kMaxRows) {
        mRows[entry.row] = entry;
        if (mRowCount < entry.rowCount) {
            mRowCount = entry.rowCount;
        }
    }
    if (mPage == Page::HISTORY) {
        render();
    }
}

void MainView::renderStatus(bool tracking)
{
    touchgfx::Unicode::strncpy(statusBuffer, tracking ? "SLEEP" : "IDLE",
                               kStatusBufferSize);
    statusText.setColor(tracking ? colorSleep() : colorIdle());
    statusText.invalidate();
}

void MainView::onSleepTimeline(const CustomMessage::SleepTimelineData& t)
{
    using Tl = CustomMessage::SleepTimelineData;
    static constexpr uint8_t kFullMask = (1u << Tl::kChunks) - 1;

    if (t.chunkCount == 0) {
        // Service has no timeline for the last night.
        mTimelineValid = false;
        mTimelineMask = 0;
        mTimelineDateKey = 0;
    } else if (t.chunkCount == Tl::kChunks && t.chunk < Tl::kChunks) {
        if (mTimelineDateKey != t.dateKey) {
            mTimelineMask = 0; // first chunk of a new night
        }
        mTimelineDateKey = t.dateKey;
        memcpy(mTimelineCols + t.chunk * (Tl::kColumnsPerChunk / 4),
               t.columns, Tl::kColumnsPerChunk / 4);
        mTimelineMask |= static_cast<uint8_t>(1u << t.chunk);
        mTimelineValid = (mTimelineMask == kFullMask);
    }
    render();
}

void MainView::renderStageBar()
{
    touchgfx::Rect cover(kBarX, kBarY, kBarW, kBarH);
    invalidateRect(cover);

    if (mTimelineValid && mTimelineDateKey == mSummary.dateKey) {
        stageBar.setColumns(mTimelineCols);
        stageBar.setVisible(true);
    } else {
        stageBar.setVisible(false);
    }
    stageBar.invalidate();
}

void MainView::hideStageBar()
{
    touchgfx::Rect cover(kBarX, kBarY, kBarW, kBarH);
    invalidateRect(cover);
    stageBar.setVisible(false);
}

void MainView::flushMainText()
{
    touchgfx::Unicode::strncpy(textBuffer, stagingBuf, kTextBufferSize);
    mainText.resizeToCurrentText();
    mainText.invalidate();
}

void MainView::renderHistory()
{
    mainText.invalidate(); // erase old rect before moving/resizing
    mainText.setXY(kTextX, kTextYHist);

    if (mRowCount == 0) {
        snprintf(stagingBuf, sizeof(stagingBuf), "WEEK HISTORY\n\nno nights yet");
    } else {
        snprintf(stagingBuf, sizeof(stagingBuf), "WEEK (%u NIGHTS)\n",
                 static_cast<unsigned>(mRowCount));
        for (uint8_t i = 0; i < mRowCount; ++i) {
            const auto& r = mRows[i];
            size_t len = strlen(stagingBuf);
            if (len >= sizeof(stagingBuf) - 20) {
                break; // out of buffer: show what fits
            }
            snprintf(stagingBuf + len, sizeof(stagingBuf) - len,
                     "%02u/%02u %luH%02lu D%luH%02lu\n",
                     static_cast<unsigned>((r.dateKey / 100) % 100),
                     static_cast<unsigned>(r.dateKey % 100),
                     static_cast<unsigned long>(r.totalMin / 60),
                     static_cast<unsigned long>(r.totalMin % 60),
                     static_cast<unsigned long>(r.deepMin / 60),
                     static_cast<unsigned long>(r.deepMin % 60));
        }
        size_t len = strlen(stagingBuf);
        snprintf(stagingBuf + len, sizeof(stagingBuf) - len, "\nL1 BACK");
    }

    flushMainText();
}

void MainView::render()
{
    const bool tracking = mHasState
        && mState.state == CustomMessage::TrackingState::TRACKING;

    renderStatus(tracking);

    if (!tracking && mPage == Page::HISTORY) {
        hideStageBar();
        renderHistory();
        return;
    }

    mainText.invalidate(); // erase old rect before moving/resizing
    mainText.setXY(kTextX, kTextY);

    if (tracking) {
        hideStageBar();
        char dur[12];
        fmtDur(mState.elapsedMin, dur, sizeof(dur));
        if (mState.liveHr > 0) {
            snprintf(stagingBuf, sizeof(stagingBuf),
                     "SLEEPING %s\nHR %u BPM\n\nR1 STOP",
                     dur, static_cast<unsigned>(mState.liveHr));
        } else {
            snprintf(stagingBuf, sizeof(stagingBuf),
                     "SLEEPING %s\nHR --\n\nR1 STOP",
                     dur);
        }
    } else if (mHasSummary && mSummary.totalMin > 0) {
        renderStageBar();
        char total[12], deep[12], light[12], awake[12], bed[8], wake[8];
        fmtDur(mSummary.totalMin, total, sizeof(total));
        fmtDur(mSummary.deepMin, deep, sizeof(deep));
        fmtDur(mSummary.lightMin, light, sizeof(light));
        fmtDur(mSummary.awakeMin, awake, sizeof(awake));
        fmtClock(mSummary.bedMin, bed, sizeof(bed));
        fmtClock(mSummary.wakeMin, wake, sizeof(wake));

        const char* endNote = "";
        if (mSummary.flags & Sleep::Flags::kAbortedUnworn) endNote = "END: UNWORN";
        else if (mSummary.flags & Sleep::Flags::kAbortedBattery) endNote = "END: BATTERY";
        else if (mSummary.flags & Sleep::Flags::kInterrupted) endNote = "END: PWR OFF";
        else if (mSummary.flags & Sleep::Flags::kAutoWake) endNote = "END: AUTO-WAKE";
        // The end note and the auto-start hint share a line to keep the
        // page to 5 lines (status field + stage bar take the top).
        const char* note = endNote[0] ? endNote : "AUTO 20-03";

        if (mSummary.hrMin > 0) {
            snprintf(stagingBuf, sizeof(stagingBuf),
                     "LAST NIGHT %s\nD %s L %s A %s\n%s-%s HR%u-%u\n%s\nR1 START L1 WK",
                     total, deep, light, awake, bed, wake,
                     static_cast<unsigned>(mSummary.hrMin),
                     static_cast<unsigned>(mSummary.hrMax),
                     note);
        } else {
            snprintf(stagingBuf, sizeof(stagingBuf),
                     "LAST NIGHT %s\nD %s L %s A %s\n%s-%s\n%s\nR1 START L1 WK",
                     total, deep, light, awake, bed, wake, note);
        }
    } else {
        hideStageBar();
        snprintf(stagingBuf, sizeof(stagingBuf),
                 "NO SLEEP YET\n\nAUTO 20-03\nR1 START L1 WK");
    }

    flushMainText();
}

void MainView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        // Page the IDLE view between last-night detail and week history.
        const bool tracking = mHasState
            && mState.state == CustomMessage::TrackingState::TRACKING;
        if (!tracking) {
            mPage = (mPage == Page::SUMMARY) ? Page::HISTORY : Page::SUMMARY;
            if (mPage == Page::HISTORY) {
                mRowCount = 0; // show "loading" until entries stream in
                presenter->historyRequest();
            }
            render();
        }
    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {
        presenter->trackingToggle();
    }

    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}
