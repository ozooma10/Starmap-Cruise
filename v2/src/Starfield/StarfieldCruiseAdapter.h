#pragma once

#include "Application/CruiseRuntime.h"
#include "Presentation/ActionPresenter.h"
#include "Starfield/StarfieldBodyResolutionSource.h"

class StarfieldCruiseAdapter final
{
public:
    static StarfieldCruiseAdapter& GetSingleton();

    [[nodiscard]] bool Initialize();

    StarfieldCruiseAdapter(const StarfieldCruiseAdapter&) = delete;
    StarfieldCruiseAdapter(StarfieldCruiseAdapter&&) = delete;
    StarfieldCruiseAdapter& operator=(const StarfieldCruiseAdapter&) = delete;
    StarfieldCruiseAdapter& operator=(StarfieldCruiseAdapter&&) = delete;

private:
    class Commands final : public CruiseCommands
    {
    public:
        bool CloseMap() override;
        bool PressCruise() override;
        bool RequestCourse(FormID courseId) override;
    };

    class ActionView final : public MapActionView
    {
    public:
        bool Apply(const MapActionPresentation& presentation) override;
    };

    StarfieldCruiseAdapter();

    static void OnUiSafeFrame();

    // Dependency order is intentional: CruiseRuntime retains references to the body source and commands for the adapter's process lifetime.
    StarfieldBodyResolutionSource bodySource_;
    Commands commands_;
    CruiseRuntime runtime_;
    ActionPresenter presenter_;
    ActionView actionView_;
    bool initialized_ {false};
};
