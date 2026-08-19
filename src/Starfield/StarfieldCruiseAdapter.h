#pragma once

#include "Application/CruiseRuntime.h"
#include "Starfield/HudObservationInbox.h"
#include "Starfield/MapActionInputState.h"
#include "Starfield/MapObservationInbox.h"
#include "Starfield/RemoteRouteBridge.h"
#include "Starfield/StationTargetBridge.h"
#include "Starfield/StarfieldBodyResolutionSource.h"
#include "Starfield/TravelObservationInbox.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace RE
{
    class BSInputEventReceiver;
    class IMenu;
    class InputEvent;
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
    class MarkersHandler;
    class DossierHandler;
    class HudCourseHandler;
    class MapActionSurface;
    class GravJumpSink;
    class LoadGameSink;

    struct SelectionTrace
    {
        MapSessionIdentity identity;
        SelectionAvailability availability {SelectionAvailability::Hidden};
        SelectionReason reason {SelectionReason::InactiveContext};
        FormID targetId {0};
        std::optional<SystemIdentity> system;
        std::string displayName;

        friend bool operator==(const SelectionTrace&, const SelectionTrace&) = default;
    };

    class Commands final : public CruiseCommands
    {
    public:
        explicit Commands(StarfieldCruiseAdapter& owner);

        bool CloseMap() override;
        bool BeginRemoteRoute(const ::BeginRemoteRoute& effect) override;
        bool AssignStationTarget(FormID targetId) override;
        bool PressCruise(OperationId operationId) override;
        bool RequestCourse(FormID courseId, OperationId operationId) override;

    private:
        StarfieldCruiseAdapter& m_owner;
    };

    struct HudSnapshot
    {
        ObservedCruiseState cruiseState {ObservedCruiseState::Unknown};
        bool engageAvailable {false};
    };

    struct InputBindings
    {
        std::int32_t keyboard {-1};
        std::int32_t keyboardModifier {-1};
        std::int32_t mouse {-1};
        std::int32_t gamepad {-1};
    };

    using Clock = std::chrono::steady_clock;
    using ProcessInputFunction = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);

    StarfieldCruiseAdapter();
    ~StarfieldCruiseAdapter();

    static void OnMovieCreated(RE::IMenu* menu);
    static void OnUiSafeFrame();
    static void ProcessInput(RE::BSInputEventReceiver* receiver, const RE::InputEvent* head);

    void DrainMapObservations();
    void DrainHudObservations();
    void DrainTravelObservations();
    void EvaluateRemoteArrival();
    void HandleRemoteRouteResult(RemoteRouteResult result);
    void ClearRemoteDispatchState(OperationId operationId);
    void ResetRemoteTravelState();
    void TraceCurrentSelection();
    void TrySubscribeMapFeeds();
    void TrySubscribeHudFeed();
    void UpdateHudRuntime();
    void RefreshInputPresentation();
    void UpdateMapAction();
    void UpdateTimeouts();
    bool InitializeTravelObservers();

    bool InstallInput();
    void ResolveInputBindings();
    void ProcessInputEvents(RE::BSInputEventReceiver* receiver, const RE::InputEvent* head);
    void ResetMapActionInput();

    MapActionEnvironment ReadMapActionEnvironment();
    HudSnapshot ReadHudSnapshot();
    bool InvokeHudCruiseUserEvent(const char* userEvent, bool down);
    bool DispatchCourse(FormID courseId, OperationId operationId);
    bool DispatchMapClose();

    bool IsCurrentMapMovie(const void* root, const MapSessionIdentity& identity);
    bool IsCurrentHudMovie(const void* root, std::uint32_t generation);

    StarfieldBodyResolutionSource m_bodySource;
    RemoteRouteBridge m_remoteRoute;
    StationTargetBridge m_stationTargets;
    Commands m_commands;
    CruiseRuntime m_runtime;
    MapObservationInbox m_mapObservations;
    HudObservationInbox m_hudObservations;
    TravelObservationInbox m_travelObservations;
    std::optional<SelectionTrace> m_lastSelectionTrace;
    std::unique_ptr<MapActionSurface> m_mapActionSurface;

    std::int64_t m_mapMovieBornTicks {0};
    MapSessionIdentity m_activeMapIdentity;
    MapSessionIdentity m_mapDataSubscriptionIdentity;
    MapSessionIdentity m_markersSubscriptionIdentity;
    MapSessionIdentity m_dossierSubscriptionIdentity;
    std::unique_ptr<MapLifecycleSink> m_mapLifecycleSink;
    std::unique_ptr<MapDataHandler> m_mapDataHandler;
    std::unique_ptr<MarkersHandler> m_markersHandler;
    std::unique_ptr<DossierHandler> m_dossierHandler;
    std::unique_ptr<HudCourseHandler> m_hudCourseHandler;
    std::unique_ptr<GravJumpSink> m_gravJumpSink;
    std::unique_ptr<LoadGameSink> m_loadGameSink;

    std::uint32_t m_hudMovieGeneration {0};
    std::int64_t m_hudMovieBornTicks {0};
    std::uint32_t m_hudSubscriptionGeneration {0};

    std::atomic<ProcessInputFunction> m_originalInput {nullptr};
    InputBindings m_inputBindings;
    std::atomic<bool> m_lastInputWasGamepad {false};
    bool m_presentedInputWasGamepad {false};
    std::atomic<bool> m_mapActionInteractive {false};
    MapActionInputState m_mapActionInput;
    std::optional<std::uint32_t> m_pendingCruiseInputDevice;

    std::optional<bool> m_lastCruiseActive;
    HudSnapshot m_hudSnapshot;
    Clock::time_point m_nextHudPoll;
    bool m_hudCruisePressed {false};
    bool m_hudCruiseTimedOut {false};
    OperationId m_hudCruiseOperationId {0};
    std::string m_hudCruiseUserEvent;
    Clock::time_point m_hudCruiseStarted;

    MapSessionIdentity m_pendingMapCloseIdentity;
    Clock::time_point m_mapCloseStarted;
    FormID m_pendingCourseId {0};
    OperationId m_pendingCourseOperationId {0};
    Clock::time_point m_courseRequestStarted;

    bool m_remoteRoutingAvailable {false};
    bool m_loadingMenuOpen {false};
    std::uint8_t m_gravJumpProgress {0};
    bool m_completedPlayerJump {false};
    std::int64_t m_lastTravelTicks {0};
    Clock::time_point m_lastRemoteUnsettled;
    Clock::time_point m_invalidFlightSince;
    Clock::time_point m_remoteCruiseInactiveSince;
    std::optional<HudObservationInbox::CourseObservation> m_lastHudCourse;

    // Hook installation is process-lifetime and intentionally not undone.
    // Once initialization starts, a partial failure is terminal so a later retry cannot treat one of our own vtable hooks as the original target.
    bool m_initializationAttempted {false};
    bool m_initialized {false};
};
