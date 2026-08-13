#include "Starfield/StarfieldCruiseAdapter.h"

StarfieldCruiseAdapter& StarfieldCruiseAdapter::GetSingleton()
{
    static StarfieldCruiseAdapter singleton;
    return singleton;
}

StarfieldCruiseAdapter::StarfieldCruiseAdapter() : runtime_(bodySource_, commands_) {}

bool StarfieldCruiseAdapter::Commands::CloseMap()
{
    return false;
}

bool StarfieldCruiseAdapter::Commands::PressCruise()
{
    return false;
}

bool StarfieldCruiseAdapter::Commands::RequestCourse(FormID)
{
    return false;
}

bool StarfieldCruiseAdapter::ActionView::Apply(const MapActionPresentation&)
{
    return false;
}
