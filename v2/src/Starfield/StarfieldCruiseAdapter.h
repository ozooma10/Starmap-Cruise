#pragma once

#include "Application/CruiseRuntime.h"
#include "Starfield/HudObservationInbox.h"
#include "Starfield/MapActionInputState.h"
#include "Starfield/MapObservationInbox.h"
#include "Starfield/StationTargetBridge.h"
#include "Starfield/StarfieldBodyResolutionSource.h"

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

    struct SelectionTrace
    {
        MapSessionIdentity identity;
        SelectionAvailability availability {SelectionAvailability::Hidden};
        SelectionReason reason {SelectionReason::InactiveContext};
        FormID targetId {0};
        std::optional<FormID> systemId;
        std::string displayName;

        friend bool operator==(const SelectionTrace&, const SelectionTrace&) = default;
    };

    class Commands final : public CruiseCommands
    {
    public:
        explicit Commands(StarfieldCruiseAdapter& owner);

        bool CloseMap() override;
        bool AssignStationTarget(FormID targetId) override;
        bool PressCruise() override;
        bool RequestCourse(FormID courseId) override;

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
    void TraceCurrentSelection();
    void TrySubscribeMapFeeds();
    void TrySubscribeHudFeed();
    void UpdateHudRuntime();
    void RefreshInputPresentation();
    void UpdateMapAction();
    void UpdateTimeouts();

    bool InstallInput();
    void ResolveInputBindings();
    void ProcessInputEvents(RE::BSInputEventReceiver* receiver, const RE::InputEvent* head);
    void ResetMapActionInput();

    MapActionEnvironment ReadMapActionEnvironment();
    HudSnapshot ReadHudSnapshot();
    bool InvokeHudCruiseUserEvent(const char* userEvent, bool down);
    bool DispatchCourse(FormID courseId);
    bool DispatchMapClose();

    bool IsCurrentMapMovie(const void* root, const MapSessionIdentity& identity);
    bool IsCurrentHudMovie(const void* root, std::uint32_t generation);

    StarfieldBodyResolutionSource m_bodySource;
    StationTargetBridge m_stationTargets;
    Commands m_commands;
    CruiseRuntime m_runtime;
    MapObservationInbox m_mapObservations;
    HudObservationInbox m_hudObservations;
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
    std::string m_hudCruiseUserEvent;
    Clock::time_point m_hudCruiseStarted;

    MapSessionIdentity m_pendingMapCloseIdentity;
    Clock::time_point m_mapCloseStarted;
    FormID m_pendingCourseId {0};
    Clock::time_point m_courseRequestStarted;

    bool m_initialized {false};
};
