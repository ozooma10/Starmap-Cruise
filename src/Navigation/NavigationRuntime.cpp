#include "Navigation/NavigationRuntime.h"
#include "Domain/NonzeroCounter.h"

#include <algorithm>
#include <utility>

TransitionResult NavigationRuntime::SelectDestination(Destination destination, SelectionIntent intent, bool cruiseAlreadyActive, RemoteSelectionContext remote)
{
    TransitionResult result;
    if (!destination.IsValid()) {
        return result;
    }

    result.handled = true;
    if (m_state.destination && m_state.destination->SameIdentityAs(destination)) {
        Reset();
        result.effect = CloseMap {};
        return result;
    }

    if (intent == SelectionIntent::StartRemoteCruise) {
        if (cruiseAlreadyActive || !remote.source.IsValid() || destination.kind == DestinationKind::Station) {
            result.handled = false;
            return result;
        }

        RemoteCruiseOperation operation {
            .id = NextOperationId(),
            .source = remote.source,
            .inputDevice = remote.inputDevice,
            .destination = destination,
        };

        m_pendingIntent.reset();
        m_cruiseWasActiveWhenSelected = false;
        m_state.destination = destination;
        m_state.remoteOperation = operation;
        m_state.phase = NavigationPhase::RoutingRemote;
        result.effect = BeginRemoteRoute {
            .operationId = operation.id,
            .source = operation.source,
            .inputDevice = operation.inputDevice,
            .destination = std::move(destination),
        };
        return result;
    }

    m_state.destination = std::move(destination);
    m_state.remoteOperation.reset();
    m_state.phase = NavigationPhase::ClosingMap;
    m_pendingIntent = intent;
    m_cruiseWasActiveWhenSelected = cruiseAlreadyActive;
    result.effect = CloseMap {};
    return result;
}

TransitionResult NavigationRuntime::MapClosed()
{
    if (m_state.phase != NavigationPhase::ClosingMap || !m_state.destination || !m_pendingIntent) {
        return {};
    }

    TransitionResult result {.handled = true};
    const auto intent = *m_pendingIntent;
    const bool cruiseWasActive = m_cruiseWasActiveWhenSelected;
    m_pendingIntent.reset();
    m_cruiseWasActiveWhenSelected = false;

    if (cruiseWasActive) {
        m_state.phase = NavigationPhase::AwaitingCourseLock;
        result.effect = RequestCourse {.courseId = m_state.destination->courseId};
        return result;
    }
    if (intent == SelectionIntent::Mark) {
        m_state.phase = NavigationPhase::Marked;
        return result;
    }

    m_state.phase = NavigationPhase::CruiseRequested;
    result.effect = PressCruise {};
    return result;
}

TransitionResult NavigationRuntime::RemoteRouteCommitted(OperationId operationId, RemoteOperationSource source)
{
    if (!OwnsRemote(operationId, source) || m_state.phase != NavigationPhase::RoutingRemote) {
        return {};
    }
    m_state.phase = NavigationPhase::PendingRemoteArrival;
    return {.handled = true};
}

TransitionResult NavigationRuntime::RemoteRouteFailed(OperationId operationId, RemoteOperationSource source)
{
    if (!OwnsRemote(operationId, source) || m_state.phase != NavigationPhase::RoutingRemote) {
        return {};
    }
    Reset();
    return {.handled = true};
}

TransitionResult NavigationRuntime::ObserveRemoteArrival(RemoteArrivalObservation observation)
{
    if (!m_state.remoteOperation || m_state.phase != NavigationPhase::PendingRemoteArrival || observation.operationId != m_state.remoteOperation->id) {
        return {};
    }

    auto& operation = *m_state.remoteOperation;
    if (!observation.mapClosed || !observation.loadingMenuClosed || (!observation.completedPlayerJump && !observation.completedReplacementJump) || !observation.settledFlight || !observation.flying || observation.currentBodyId == 0 ||
        observation.currentSystem != operation.destination.system) {
        return {};
    }

    if (observation.currentBodyId == operation.destination.targetId) {
        Reset();
        return {.handled = true};
    }

    if (!observation.freshHudPublication) {
        return {};
    }

    if (!observation.courseRowsComplete) {
        Reset();
        return {.handled = true};
    }

    const auto finalCount = static_cast<std::size_t>(std::count(observation.courseRows.begin(), observation.courseRows.end(), operation.destination.courseId));
    if (finalCount > 1) {
        Reset();
        return {.handled = true};
    }

    if (finalCount == 0) {
        std::size_t waypointMatches = 0;
        std::size_t waypointIndex = 0;
        for (const auto row : observation.courseRows) {
            const auto found = std::find(operation.destination.remotePlan.allowedWaypointIds.begin(), operation.destination.remotePlan.allowedWaypointIds.end(), row);
            if (found == operation.destination.remotePlan.allowedWaypointIds.end()) {
                continue;
            }
            ++waypointMatches;
            waypointIndex = static_cast<std::size_t>(found - operation.destination.remotePlan.allowedWaypointIds.begin());
        }
        if (waypointMatches > 1) {
            Reset();
            return {.handled = true};
        }
        if (waypointMatches != 1 || waypointIndex != 0) {
            return {};
        }
        operation.nextWaypointIndex = 0;
    } else {
        operation.nextWaypointIndex = operation.destination.remotePlan.allowedWaypointIds.size();
    }

    m_state.phase = NavigationPhase::PreparingRemoteTarget;
    return {
        .handled = true,
        .effect = PressCruise {.operationId = operation.id},
    };
}

TransitionResult NavigationRuntime::CruiseChanged(bool active)
{
    if (!m_state.destination) {
        return {};
    }

    if (active && m_state.phase == NavigationPhase::PreparingRemoteTarget && m_state.remoteOperation) {
        m_state.phase = NavigationPhase::AwaitingCourseLock;
        return {
            .handled = true,
            .effect = RequestCourse {
                .courseId = m_state.destination->courseId,
                .operationId = m_state.remoteOperation->id,
            },
        };
    }

    if (active && (m_state.phase == NavigationPhase::Marked || m_state.phase == NavigationPhase::CruiseRequested)) {
        m_state.phase = NavigationPhase::AwaitingCourseLock;
        return {
            .handled = true,
            .effect = RequestCourse {.courseId = m_state.destination->courseId},
        };
    }

    if (!active && !m_state.remoteOperation && (m_state.phase == NavigationPhase::AwaitingCourseLock || m_state.phase == NavigationPhase::CourseLocked)) {
        m_state.phase = NavigationPhase::Marked;
        return {.handled = true};
    }
    return {};
}

TransitionResult NavigationRuntime::CourseLockChanged(FormID lockedCourseId)
{
    if (!m_state.destination) {
        return {};
    }

    const auto expectedCourseId = m_state.destination->courseId;
    if (m_state.remoteOperation && m_state.phase == NavigationPhase::AwaitingCourseLock) {
        auto& operation = *m_state.remoteOperation;
        if (lockedCourseId == expectedCourseId) {
            m_state.phase = NavigationPhase::CourseLocked;
            m_state.remoteOperation.reset();
            return {.handled = true};
        }
        if (lockedCourseId == 0) {
            return {};
        }

        const auto& waypoints = operation.destination.remotePlan.allowedWaypointIds;
        if (operation.nextWaypointIndex < waypoints.size() && lockedCourseId == waypoints[operation.nextWaypointIndex]) {
            ++operation.nextWaypointIndex;
            return {.handled = true};
        }
        if (operation.nextWaypointIndex != 0 && lockedCourseId == waypoints[operation.nextWaypointIndex - 1]) {
            return {.handled = true};
        }

        Reset();
        return {.handled = true};
    }

    if (lockedCourseId == expectedCourseId && (m_state.phase == NavigationPhase::Marked || m_state.phase == NavigationPhase::AwaitingCourseLock || m_state.phase == NavigationPhase::CourseLocked)) {
        m_state.phase = NavigationPhase::CourseLocked;
        return {.handled = true};
    }
    if (m_state.phase == NavigationPhase::CourseLocked && lockedCourseId != expectedCourseId) {
        m_state.phase = NavigationPhase::Marked;
        return {.handled = true};
    }
    return {};
}

bool NavigationRuntime::InvalidateMapSelection()
{
    if (m_state.phase != NavigationPhase::ClosingMap && m_state.phase != NavigationPhase::RoutingRemote) {
        return false;
    }
    Reset();
    return true;
}

bool NavigationRuntime::CancelRemoteOperation(OperationId operationId)
{
    if (!m_state.remoteOperation || m_state.remoteOperation->id != operationId) {
        return false;
    }
    Reset();
    return true;
}

bool NavigationRuntime::InvalidateRemoteFlight()
{
    if (!m_state.remoteOperation) {
        return false;
    }
    Reset();
    return true;
}

bool NavigationRuntime::ResetForLoad()
{
    const bool changed = m_state.phase != NavigationPhase::Idle || m_state.destination.has_value();
    Reset();
    return changed;
}

bool NavigationRuntime::RemoteCruiseExitTimedOut(OperationId operationId)
{
    if (!m_state.remoteOperation || m_state.remoteOperation->id != operationId || (m_state.phase != NavigationPhase::PreparingRemoteTarget && m_state.phase != NavigationPhase::AwaitingCourseLock)) {
        return false;
    }
    Reset();
    return true;
}

bool NavigationRuntime::RecoverFromEffectFailure(const Effect& effect)
{
    if (std::holds_alternative<CloseMap>(effect)) {
        if (m_state.phase != NavigationPhase::ClosingMap) {
            return false;
        }
        Reset();
        return true;
    }

    if (const auto* begin = std::get_if<BeginRemoteRoute>(&effect)) {
        if (!OwnsRemote(begin->operationId, begin->source) || m_state.phase != NavigationPhase::RoutingRemote) {
            return false;
        }
        Reset();
        return true;
    }

    if (const auto* press = std::get_if<PressCruise>(&effect)) {
        if (press->operationId != 0) {
            if (!m_state.remoteOperation || m_state.remoteOperation->id != press->operationId || m_state.phase != NavigationPhase::PreparingRemoteTarget) {
                return false;
            }
            Reset();
            return true;
        }
        if (m_state.phase != NavigationPhase::CruiseRequested || !m_state.destination) {
            return false;
        }
        m_state.phase = NavigationPhase::Marked;
        return true;
    }

    const auto* request = std::get_if<RequestCourse>(&effect);
    if (!request || m_state.phase != NavigationPhase::AwaitingCourseLock || !m_state.destination || request->courseId != m_state.destination->courseId) {
        return false;
    }
    if (request->operationId != 0) {
        if (!m_state.remoteOperation || m_state.remoteOperation->id != request->operationId) {
            return false;
        }
        Reset();
        return true;
    }
    if (m_state.remoteOperation) {
        return false;
    }
    m_state.phase = NavigationPhase::Marked;
    return true;
}

void NavigationRuntime::Reset()
{
    m_state = {};
    m_pendingIntent.reset();
    m_cruiseWasActiveWhenSelected = false;
}

const NavigationState& NavigationRuntime::CurrentState() const
{
    return m_state;
}

OperationId NavigationRuntime::NextOperationId()
{
    m_nextOperationId = CFS::AdvanceNonzeroCounter(m_nextOperationId);
    return m_nextOperationId;
}

bool NavigationRuntime::OwnsRemote(OperationId operationId, RemoteOperationSource source) const
{
    return m_state.remoteOperation && m_state.remoteOperation->id == operationId && m_state.remoteOperation->source == source;
}
