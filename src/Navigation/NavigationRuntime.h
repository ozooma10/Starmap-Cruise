#pragma once

#include "Domain/Destination.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

using OperationId = std::uint64_t;

enum class NavigationPhase : std::uint8_t
{
    Idle,
    ClosingMap,
    Marked,
    RoutingRemote,
    PendingRemoteArrival,
    PreparingRemoteTarget,
    CruiseRequested,
    AwaitingCourseLock,
    CourseLocked,
};

enum class SelectionIntent : std::uint8_t
{
    Mark,
    StartCruise,
    StartRemoteCruise,
};

enum class NavigationInputDevice : std::uint8_t
{
    KeyboardMouse,
    Gamepad,
};

struct RemoteOperationSource
{
    std::uint32_t session {0};
    std::uint32_t movieGeneration {0};

    bool IsValid() const
    {
        return session != 0 && movieGeneration != 0;
    }

    friend bool operator==(const RemoteOperationSource&, const RemoteOperationSource&) = default;
};

struct RemoteSelectionContext
{
    RemoteOperationSource source;
    NavigationInputDevice inputDevice {NavigationInputDevice::KeyboardMouse};
};

struct RemoteCruiseOperation
{
    OperationId id {0};
    RemoteOperationSource source;
    NavigationInputDevice inputDevice {NavigationInputDevice::KeyboardMouse};
    Destination destination;
    std::size_t nextWaypointIndex {0};

    bool IsValid() const
    {
        return id != 0 && source.IsValid() && destination.IsValid() && destination.kind != DestinationKind::Station;
    }
};

struct NavigationState
{
    NavigationPhase phase {NavigationPhase::Idle};
    std::optional<Destination> destination;
    std::optional<RemoteCruiseOperation> remoteOperation;
};

struct CloseMap
{};

struct BeginRemoteRoute
{
    OperationId operationId {0};
    RemoteOperationSource source;
    NavigationInputDevice inputDevice {NavigationInputDevice::KeyboardMouse};
    Destination destination;
};

struct PressCruise
{
    OperationId operationId {0};
};

struct RequestCourse
{
    FormID courseId {0};
    OperationId operationId {0};
};

using Effect = std::variant<CloseMap, BeginRemoteRoute, PressCruise, RequestCourse>;

struct TransitionResult
{
    bool handled {false};
    std::optional<Effect> effect;
};

struct RemoteArrivalObservation
{
    OperationId operationId {0};
    SystemIdentity currentSystem;
    bool mapClosed {false};
    bool loadingMenuClosed {false};
    bool completedPlayerJump {false};
    bool settledFlight {false};
    bool flying {false};
    bool freshHudPublication {false};
    std::vector<FormID> courseRows;
};

class NavigationRuntime
{
public:
    TransitionResult SelectDestination(Destination destination, SelectionIntent intent, bool cruiseAlreadyActive, RemoteSelectionContext remote = {});
    TransitionResult MapClosed();
    TransitionResult RemoteRouteCommitted(OperationId operationId, RemoteOperationSource source);
    TransitionResult RemoteRouteFailed(OperationId operationId, RemoteOperationSource source);
    TransitionResult ObserveRemoteArrival(RemoteArrivalObservation observation);
    TransitionResult CruiseChanged(bool active);
    TransitionResult CourseLockChanged(FormID lockedCourseId);

    bool InvalidateMapSelection();
    bool CancelRemoteOperation(OperationId operationId);
    bool InvalidateRemoteFlight();
    bool ResetForLoad();
    bool RemoteCruiseExitTimedOut(OperationId operationId);
    bool RecoverFromEffectFailure(const Effect& effect);

    void Reset();
    const NavigationState& CurrentState() const;

private:
    OperationId NextOperationId();
    bool OwnsRemote(OperationId operationId, RemoteOperationSource source) const;

    NavigationState m_state;
    OperationId m_nextOperationId {0};
    std::optional<SelectionIntent> m_pendingIntent;
    bool m_cruiseWasActiveWhenSelected {false};
};
