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

    /** Renders the overnight probe summary into the main text area. */
    void onProbeStats(const CustomMessage::ProbeStatsData& stats);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    // Probe readout: replaces the Designer's single-use "Hello World" text
    // (textArea1 is removed in setupScreen — no Designer round-trip needed).
    // This TouchGFX port stores a pointer to the wildcard, not a copy —
    // probeTextBuffer must outlive the widget (it's a member, so it does).
    static constexpr uint16_t kProbeTextBufferSize = 192;
    touchgfx::TextAreaWithOneWildcard probeText;
    touchgfx::Unicode::UnicodeChar probeTextBuffer[kProbeTextBufferSize];
};

#endif // MAINVIEW_HPP
