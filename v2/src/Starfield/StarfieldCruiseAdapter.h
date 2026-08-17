#pragma once

#include "Application/CruiseRuntime.h"
#include "Starfield/MapObservationInbox.h"
#include "Starfield/StarfieldBodyResolutionSource.h"

#include <cstdint>
#include <memory>

namespace RE
{
    class IMenu;
}

class StarfieldCruiseAdapter final
{
public:
    static StarfieldCruiseAdapter& GetSingleton();

    bool Initialize();

    StarfieldCruiseAdapter(const StarfieldCruiseAdapter&) = delete;
    StarfieldCruiseAdapter(StarfieldCruiseAdapter&&) = delete;
    StarfieldCruiseAdapter& operator=(const StarfieldCruiseAdapter&) = delete;
    StarfieldCruiseAdapter& operator=(StarfieldCruiseAdapter&&) = delete;

private:
    class MapLifecycleSink;
    class MapDataHandler;

    class Commands final : public CruiseCommands
    {
    public:
        bool CloseMap() override;
        bool PressCruise() override;
        bool RequestCourse(FormID courseId) override;
    };

    StarfieldCruiseAdapter();
    ~StarfieldCruiseAdapter();

    static void OnMovieCreated(RE::IMenu* menu);
    static void OnUiSafeFrame();

    void DrainMapObservations();
    void TrySubscribeMapView();

    bool IsCurrentMapMovie(const void* root, const MapSessionIdentity& identity);

    StarfieldBodyResolutionSource m_bodySource;
    Commands m_commands;
    CruiseRuntime m_runtime;
    MapObservationInbox m_mapObservations;

    std::int64_t m_mapMovieBornTicks {0};
    MapSessionIdentity m_activeMapIdentity;
    MapSessionIdentity m_mapViewSubscriptionIdentity;
    std::unique_ptr<MapLifecycleSink> m_mapLifecycleSink;
    std::unique_ptr<MapDataHandler> m_mapDataHandler;
    bool m_initialized {false};
};
