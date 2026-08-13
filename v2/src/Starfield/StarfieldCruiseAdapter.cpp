#include "Starfield/StarfieldCruiseAdapter.h"

#include "MainThreadUiPump.h"

#include "REX/REX.h"

StarfieldCruiseAdapter& StarfieldCruiseAdapter::GetSingleton()
{
    static StarfieldCruiseAdapter singleton;
    return singleton;
}

StarfieldCruiseAdapter::StarfieldCruiseAdapter() : runtime_(bodySource_, commands_) {}

bool StarfieldCruiseAdapter::Initialize()
{
    if (initialized_) {
        return true;
    }

    if (!CFS::MainThreadUiPump::Install(&OnUiSafeFrame)) {
        REX::ERROR("StarfieldCruiseAdapter: post-advance UI pump unavailable; OSF UI disabled");
        return false;
    }

    initialized_ = true;
    REX::INFO("StarfieldCruiseAdapter: initialized with inert post-advance ownership");
    return true;
}

void StarfieldCruiseAdapter::OnUiSafeFrame()
{
}

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
