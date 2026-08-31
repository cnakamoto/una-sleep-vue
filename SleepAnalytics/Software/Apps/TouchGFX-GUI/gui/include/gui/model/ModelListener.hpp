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

    /** Overnight probe summary, pushed by the service when the GUI starts. */
    virtual void onProbeStats(const CustomMessage::ProbeStatsData&) {}

protected:
    Model* model;

    
};

#endif // MODELLISTENER_HPP
