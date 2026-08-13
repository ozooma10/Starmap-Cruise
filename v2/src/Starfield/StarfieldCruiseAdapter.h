#pragma once

#include "Application/CruiseRuntime.h"
#include "Presentation/ActionPresenter.h"
#include "Starfield/StarfieldBodyResolutionSource.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

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
    struct MapLifecycleObservation
    {
        bool opening {false};
        MapSessionIdentity identity;
    };

    struct MapViewObservation
    {
        MapSessionIdentity identity;
        MapView view {MapView::Unknown};
    };

    class MapLifecycleSink;
    class MapDataHandler;

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
    ~StarfieldCruiseAdapter();

    static void OnMovieCreated(RE::IMenu* menu);
    static void OnUiSafeFrame();

    void RecordMapLifecycleObservation(bool opening);
    void RecordMapViewObservation(const MapSessionIdentity& identity, MapView view);
    void DrainMapMovieObservation();
    void DrainMapLifecycleObservations();
    void TrySubscribeMapView();
    void DrainMapViewObservation();

    bool IsCurrentMapMovie(const void* root, const MapSessionIdentity& identity);

    StarfieldBodyResolutionSource bodySource_;
    Commands commands_;
    CruiseRuntime runtime_;
    ActionPresenter presenter_;
    ActionView actionView_;
    std::atomic<std::uint64_t> mapMovieSequence_ {0};
    std::atomic<std::int64_t> mapMovieBornTicks_ {0};
    std::uint64_t consumedMapMovieSequence_ {0};
    std::mutex mapObservationMutex_;
    static constexpr std::size_t MaxPendingMapLifecycleObservations = 16;
    std::array<MapLifecycleObservation, MaxPendingMapLifecycleObservations> pendingMapLifecycle_;
    std::size_t pendingMapLifecycleCount_ {0};
    bool mapLifecycleOverflow_ {false};
    std::uint64_t mapSessionSequence_ {0};
    MapSessionIdentity publishedMapIdentity_;
    MapSessionIdentity activeMapIdentity_;
    std::optional<MapViewObservation> pendingMapView_;
    MapSessionIdentity mapViewSubscriptionIdentity_;
    std::unique_ptr<MapLifecycleSink> mapLifecycleSink_;
    std::unique_ptr<MapDataHandler> mapDataHandler_;
    bool initialized_ {false};
};
