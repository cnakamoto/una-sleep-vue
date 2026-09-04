#include <gui/main_screen/MainView.hpp>
#include <gui/main_screen/MainPresenter.hpp>

MainPresenter::MainPresenter(MainView& v)
    : view(v)
{

}

void MainPresenter::activate()
{

}

void MainPresenter::deactivate()
{

}

void MainPresenter::onSessionState(const CustomMessage::SessionStateData& state)
{
    view.onSessionState(state);
}

void MainPresenter::onSleepSummary(const CustomMessage::SleepSummaryData& summary)
{
    view.onSleepSummary(summary);
}

void MainPresenter::onHistoryEntry(const CustomMessage::HistoryEntryData& entry)
{
    view.onHistoryEntry(entry);
}

void MainPresenter::onSleepTimeline(const CustomMessage::SleepTimelineData& timeline)
{
    view.onSleepTimeline(timeline);
}
