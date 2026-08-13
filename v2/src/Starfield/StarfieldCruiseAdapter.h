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

    class MapLifecycleSink;

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
    void DrainMapMovieObservation();
    void DrainMapLifecycleObservations();

    StarfieldBodyResolutionSource bodySource_;
    Commands commands_;
    CruiseRuntime runtime_;
    ActionPresenter presenter_;
    ActionView actionView_;
    std::atomic<std::uint64_t> mapMovieSequence_ {0};
    std::uint64_t consumedMapMovieSequence_ {0};
    std::mutex mapLifecycleMutex_;
    static constexpr std::size_t MaxPendingMapLifecycleObservations = 16;
    std::array<MapLifecycleObservation, MaxPendingMapLifecycleObservations> pendingMapLifecycle_;
    std::size_t pendingMapLifecycleCount_ {0};
    bool mapLifecycleOverflow_ {false};
    std::uint64_t mapSessionSequence_ {0};
    MapSessionIdentity publishedMapIdentity_;
    std::unique_ptr<MapLifecycleSink> mapLifecycleSink_;
    bool initialized_ {false};
};
