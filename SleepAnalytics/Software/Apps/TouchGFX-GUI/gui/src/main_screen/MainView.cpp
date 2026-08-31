#include <gui/main_screen/MainView.hpp>
#include <texts/TextKeysAndLanguages.hpp>

#include <cstdio>

namespace
{

// 452 min -> "7H32M"
void formatMinutes(uint32_t minutes, char* out, size_t outSize)
{
    snprintf(out, outSize, "%luH%02luM",
             static_cast<unsigned long>(minutes / 60),
             static_cast<unsigned long>(minutes % 60));
}

} // namespace

MainView::MainView()
{
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    buttons.setL1(ButtonsSet::NONE);
    buttons.setL2(ButtonsSet::NONE);
    buttons.setR1(ButtonsSet::NONE);
    buttons.setR2(ButtonsSet::WHITE);

    // Replace the Designer's single-use "Hello World" text with a wildcard
    // text area for the probe readout (no Designer round-trip needed).
    remove(textArea1);

    probeText.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_18_L));
    probeText.setXY(16, 24);
    probeText.setWidth(208);
    probeText.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    probeText.setLinespacing(2);
    probeText.setWildcard1(probeTextBuffer);
    add(probeText);

    touchgfx::Unicode::snprintf(probeTextBuffer, kProbeTextBufferSize,
                                "PROBE RUNNING\nwear overnight\nopen in morning");
    probeText.resizeToCurrentText();
    probeText.invalidate();
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::onProbeStats(const CustomMessage::ProbeStatsData& s)
{
    if (s.firstEpoch == 0) {
        touchgfx::Unicode::snprintf(probeTextBuffer, kProbeTextBufferSize,
                                    "NO PROBE DATA\nlog file empty");
    } else {
        char span[12];
        char up[12];
        formatMinutes((s.lastEpoch - s.firstEpoch) / 60, span, sizeof(span));
        formatMinutes(s.upMin, up, sizeof(up));

        if (s.battFirstD >= 0 && s.battLastD >= 0) {
            touchgfx::Unicode::snprintf(
                probeTextBuffer, kProbeTextBufferSize,
                "PROBE %s\nBOOTS %u STOP %u\nALIVE %u HR %lu\nMAXGAP %luS\nBATT %d>%d\nUP %s",
                span,
                static_cast<unsigned>(s.boots),
                static_cast<unsigned>(s.stops),
                static_cast<unsigned>(s.aliveCount),
                static_cast<unsigned long>(s.hrSamples),
                static_cast<unsigned long>(s.maxGapSec),
                static_cast<int>(s.battFirstD / 10),
                static_cast<int>(s.battLastD / 10),
                up);
        } else {
            touchgfx::Unicode::snprintf(
                probeTextBuffer, kProbeTextBufferSize,
                "PROBE %s\nBOOTS %u STOP %u\nALIVE %u HR %lu\nMAXGAP %luS\nUP %s",
                span,
                static_cast<unsigned>(s.boots),
                static_cast<unsigned>(s.stops),
                static_cast<unsigned>(s.aliveCount),
                static_cast<unsigned long>(s.hrSamples),
                static_cast<unsigned long>(s.maxGapSec),
                up);
        }
    }

    // probeText points at probeTextBuffer already — just re-layout + redraw.
    probeText.resizeToCurrentText();
    probeText.invalidate();
}

void MainView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {

    }

    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}
