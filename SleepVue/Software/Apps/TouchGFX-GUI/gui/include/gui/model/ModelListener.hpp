#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>
#include <gui/common/FrontendApplication.hpp>

class ModelListener
{
public:
    ModelListener() : model(0) {}
    
    virtual ~ModelListener() {}

    void bind(Model* m)
    {
        model = m;
    }

    virtual void onIdleTimeout() {}

    /** Tracking state changed (or periodic refresh while TRACKING). */
    virtual void onSessionState(const CustomMessage::SessionStateData&) {}

    /** Last completed night's summary arrived. */
    virtual void onSleepSummary(const CustomMessage::SleepSummaryData&) {}

    /** One history row arrived (streamed after HistoryRequest). */
    virtual void onHistoryEntry(const CustomMessage::HistoryEntryData&) {}

    /** One stage-timeline chunk arrived (streamed on GUI start / close). */
    virtual void onSleepTimeline(const CustomMessage::SleepTimelineData&) {}

protected:
    Model* model;

    
};

#endif // MODELLISTENER_HPP
