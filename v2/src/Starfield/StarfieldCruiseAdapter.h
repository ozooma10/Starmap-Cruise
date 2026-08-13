#pragma once

#include "Application/CruiseRuntime.h"
#include "Presentation/ActionPresenter.h"
#include "Starfield/StarfieldBodyResolutionSource.h"

#include <atomic>
#include <cstdint>

namespace RE
{
    class IMenu;
}

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

    static void OnMovieCreated(RE::IMenu* menu);
    static void OnUiSafeFrame();

    void DrainMapMovieObservation();

    // Dependency order is intentional: CruiseRuntime retains references to the body source and commands for the adapter's process lifetime.
    StarfieldBodyResolutionSource bodySource_;
    Commands commands_;
    CruiseRuntime runtime_;
    ActionPresenter presenter_;
    ActionView actionView_;
    std::atomic<std::uint64_t> mapMovieSequence_ {0};
    std::uint64_t consumedMapMovieSequence_ {0};
    bool initialized_ {false};
};
