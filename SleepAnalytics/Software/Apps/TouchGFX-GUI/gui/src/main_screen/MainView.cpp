#include <gui/main_screen/MainView.hpp>
#include <texts/TextKeysAndLanguages.hpp>

#include "SleepTypes.hpp"

#include <cstdio>

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
    mainText.setXY(16, 24);
    mainText.setWidth(208);
    mainText.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    mainText.setLinespacing(2);
    mainText.setWildcard1(textBuffer);
    add(mainText);

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

void MainView::renderHistory()
{
    if (mRowCount == 0) {
        touchgfx::Unicode::snprintf(textBuffer, kTextBufferSize,
                                    "WEEK HISTORY\n\nno nights yet");
    } else {
        touchgfx::Unicode::snprintf(textBuffer, kTextBufferSize,
                                    "WEEK (%u NIGHTS)\n",
                                    static_cast<unsigned>(mRowCount));
        for (uint8_t i = 0; i < mRowCount; ++i) {
            const auto& r = mRows[i];
            uint16_t len = touchgfx::Unicode::strlen(textBuffer);
            if (len >= kTextBufferSize - 20) {
                break; // out of buffer: show what fits
            }
            touchgfx::Unicode::snprintf(
                textBuffer + len, kTextBufferSize - len,
                "%02u/%02u %luH%02lu D%luH%02lu\n",
                static_cast<unsigned>((r.dateKey / 100) % 100),
                static_cast<unsigned>(r.dateKey % 100),
                static_cast<unsigned long>(r.totalMin / 60),
                static_cast<unsigned long>(r.totalMin % 60),
                static_cast<unsigned long>(r.deepMin / 60),
                static_cast<unsigned long>(r.deepMin % 60));
        }
        uint16_t len = touchgfx::Unicode::strlen(textBuffer);
        touchgfx::Unicode::snprintf(textBuffer + len, kTextBufferSize - len,
                                    "\nL1 BACK");
    }

    mainText.resizeToCurrentText();
    mainText.invalidate();
}

void MainView::render()
{
    const bool tracking = mHasState
        && mState.state == CustomMessage::TrackingState::TRACKING;

    if (!tracking && mPage == Page::HISTORY) {
        renderHistory();
        return;
    }

    if (tracking) {
        char dur[12];
        fmtDur(mState.elapsedMin, dur, sizeof(dur));
        if (mState.liveHr > 0) {
            touchgfx::Unicode::snprintf(
                textBuffer, kTextBufferSize,
                "SLEEPING %s\nHR %u BPM\n\nR1 STOP",
                dur, static_cast<unsigned>(mState.liveHr));
        } else {
            touchgfx::Unicode::snprintf(
                textBuffer, kTextBufferSize,
                "SLEEPING %s\nHR --\n\nR1 STOP",
                dur);
        }
    } else if (mHasSummary && mSummary.totalMin > 0) {
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

        if (mSummary.hrMin > 0) {
            touchgfx::Unicode::snprintf(
                textBuffer, kTextBufferSize,
                "LAST NIGHT %s\nDEEP %s AW %s\nLIGHT %s\n%s-%s HR%u-%u\n%s\nR1 START L1 WK",
                total, deep, awake, light, bed, wake,
                static_cast<unsigned>(mSummary.hrMin),
                static_cast<unsigned>(mSummary.hrMax),
                endNote);
        } else {
            touchgfx::Unicode::snprintf(
                textBuffer, kTextBufferSize,
                "LAST NIGHT %s\nDEEP %s AW %s\nLIGHT %s\n%s-%s\n%s\nR1 START L1 WK",
                total, deep, awake, light, bed, wake, endNote);
        }
    } else {
        touchgfx::Unicode::snprintf(textBuffer, kTextBufferSize,
                                    "NO SLEEP YET\n\nR1 START L1 WK");
    }

    mainText.resizeToCurrentText();
    mainText.invalidate();
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
