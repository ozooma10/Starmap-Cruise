#pragma once

#include "Application/BodyResolutionSource.h"
#include "Map/MapSessionState.h"
#include "Navigation/NavigationRuntime.h"
#include "Presentation/ActionPolicy.h"

#include <cstdint>
#include <optional>

class CruiseCommands
{
public:
    virtual ~CruiseCommands() = default;

    virtual bool CloseMap() = 0;
    virtual bool BeginRemoteRoute(const ::BeginRemoteRoute& effect) = 0;
    virtual bool AssignStationTarget(FormID targetId) = 0;
    virtual bool PressCruise(OperationId operationId) = 0;
    virtual bool RequestCourse(FormID courseId, OperationId operationId) = 0;
};

struct EffectDispatchResult
{
    bool handled {false};
    bool targetAssignmentFailed {false};
    std::optional<Effect> failedEffect;

    bool Succeeded() const
    {
        return handled && !targetAssignmentFailed && !failedEffect.has_value();
    }
};

enum class MapActionGesture : std::uint8_t
{
    Tap,
    HoldCompleted,
};

struct MapActionEnvironment
{
    bool cruiseControlBound {false};
    bool cruiseEngageAvailable {false};
    bool vanillaActionEnabled {false};
    bool remoteRoutingAvailable {false};
    NavigationInputDevice inputDevice {NavigationInputDevice::KeyboardMouse};
};

class CruiseRuntime
{
public:
    CruiseRuntime(const BodyResolutionSource& bodySource, CruiseCommands& commands);

    void OnMapMovieCreated(std::uint32_t generation);

    bool OnMapOpened(const MapOpenContext& context);
    EffectDispatchResult OnMapClosed(const MapSessionIdentity& identity);
    bool OnMapCloseTimedOut(const MapSessionIdentity& identity);

    bool OnMapViewChanged(const MapSessionIdentity& identity, MapView view);

    bool OnMarkersChanged(const MapSessionIdentity& identity, MarkerUpdate update);

    bool OnDossierChanged(const MapSessionIdentity& identity, const TargetObservation& dossier);

    bool OnCurrentSystemResolved(const MapSessionIdentity& identity, FormID systemId);
    bool OnCurrentSystemFormObserved(const MapSessionIdentity& identity, FormID systemFormId);

    SelectionDecision CurrentSelection() const;
    ActionDecision CurrentMapAction(const MapActionEnvironment& environment) const;
    ShipContext CurrentMapShipContext() const;

    EffectDispatchResult ActivateMapAction(const MapSessionIdentity& identity, MapActionGesture gesture, const MapActionEnvironment& environment);

    EffectDispatchResult OnRemoteRouteCommitted(OperationId operationId, const MapSessionIdentity& identity);
    EffectDispatchResult OnRemoteRouteFailed(OperationId operationId, const MapSessionIdentity& identity);
    EffectDispatchResult OnRemoteArrival(RemoteArrivalObservation observation);
    bool OnRemoteOperationCancelled(OperationId operationId);
    bool OnRemoteFlightInvalidated();
    bool OnLoadGame();
    bool OnRemoteCruiseExitTimedOut(OperationId operationId);

    EffectDispatchResult OnCruiseChanged(bool active);
    bool OnCruiseActivationTimedOut(OperationId operationId = 0);

    EffectDispatchResult OnCourseLockChanged(FormID lockedCourseId);
    bool OnCourseLockTimedOut(FormID courseId, OperationId operationId = 0);

    const NavigationState& CurrentNavigationState() const;

private:
    EffectDispatchResult Execute(TransitionResult transition);

    MapSessionState m_map;
    NavigationRuntime m_navigation;
    const BodyResolutionSource& m_bodySource;
    CruiseCommands& m_commands;
};
