#include "Application/CruiseRuntime.h"

#include <utility>
#include <variant>

namespace
{
    bool IsSupported(ObservedTargetKind kind)
    {
        return kind == ObservedTargetKind::Planet || kind == ObservedTargetKind::Moon;
    }

    struct DispatchVisitor
    {
        CruiseCommands& commands;

        bool operator()(const ::CloseMap&) const
        {
            return commands.CloseMap();
        }

        bool operator()(const ::BeginRemoteRoute& effect) const
        {
            return commands.BeginRemoteRoute(effect);
        }

        bool operator()(const ::PressCruise& effect) const
        {
            return commands.PressCruise(effect.operationId);
        }

        bool operator()(const ::RequestCourse& effect) const
        {
            return commands.RequestCourse(effect.courseId, effect.operationId, effect.followsCruiseActivation);
        }
    };
}

CruiseRuntime::CruiseRuntime(const BodyResolutionSource& bodySource, CruiseCommands& commands) : m_bodySource(bodySource), m_commands(commands) {}

void CruiseRuntime::OnMapMovieCreated(std::uint32_t generation)
{
    m_navigation.InvalidateMapSelection();
    m_map.BeginMovie(generation);
}

bool CruiseRuntime::OnMapOpened(const MapOpenContext& context)
{
    return m_map.Open(context);
}

EffectDispatchResult CruiseRuntime::OnMapClosed(const MapSessionIdentity& identity)
{
    if (!m_map.Close(identity)) {
        return {};
    }

    const auto& navigation = m_navigation.CurrentState();
    if (navigation.phase == NavigationPhase::ClosingMap && navigation.destination && navigation.destination->kind == DestinationKind::Station && !m_commands.AssignStationTarget(navigation.destination->targetId)) { 
        m_navigation.Reset();
        return {
            .handled = true,
            .targetAssignmentFailed = true,
        };
    }

    return Execute(m_navigation.MapClosed());
}

bool CruiseRuntime::OnMapCloseTimedOut(const MapSessionIdentity& identity)
{
    if (!m_map.IsActive(identity)) {
        return false;
    }

    return m_navigation.RecoverFromEffectFailure(CloseMap {});
}

bool CruiseRuntime::OnMapViewChanged(const MapSessionIdentity& identity, MapView view)
{
    return m_map.SetView(identity, view);
}

bool CruiseRuntime::OnMarkersChanged(const MapSessionIdentity& identity, MarkerUpdate update)
{
    return m_map.SetMarkers(identity, std::move(update));
}

bool CruiseRuntime::OnDossierChanged(const MapSessionIdentity& identity, const TargetObservation& dossier)
{
    if (!m_map.IsActive(identity)) {
        return false;
    }

    std::optional<ResolvedBody> resolvedBody;

    if (dossier.id != 0 && IsSupported(dossier.kind)) {
        resolvedBody = m_bodySource.ResolveBody(dossier.id);
    }

    return m_map.SetDossier(identity, dossier, std::move(resolvedBody));
}

bool CruiseRuntime::OnCurrentSystemResolved(const MapSessionIdentity& identity, FormID systemId)
{
    return m_map.CaptureCurrentSystem(identity, systemId);
}

bool CruiseRuntime::OnCurrentSystemFormObserved(const MapSessionIdentity& identity, FormID systemFormId)
{
    return m_map.CaptureCurrentSystemForm(identity, systemFormId);
}

SelectionDecision CruiseRuntime::CurrentSelection() const
{
    return EvaluateSelection(m_map.Snapshot());
}

ActionDecision CruiseRuntime::CurrentMapAction(const MapActionEnvironment& environment) const
{
    const ActionContext context {
        .cruiseControlBound = environment.cruiseControlBound,
        .cruiseStateWhenMapOpened = m_map.CruiseStateWhenOpened(),
        .cruiseEngageAvailable = environment.cruiseEngageAvailable,
        .vanillaActionEnabled = environment.vanillaActionEnabled,
        .remoteRoutingAvailable = environment.remoteRoutingAvailable,
    };

    return EvaluateAction(CurrentSelection(), context);
}

ShipContext CruiseRuntime::CurrentMapShipContext() const
{
    return m_map.ShipContextWhenOpened();
}

EffectDispatchResult CruiseRuntime::ActivateMapAction(const MapSessionIdentity& identity, MapActionGesture gesture, const MapActionEnvironment& environment)
{
    if (!m_map.IsActive(identity)) {
        return {};
    }

    const auto action = CurrentMapAction(environment);

    if (!action.CanHandleInput()) {
        return {};
    }

    SelectionIntent intent = action.requiresTravel ? SelectionIntent::StartRemoteCruise : SelectionIntent::Mark;

    if (gesture == MapActionGesture::HoldCompleted) {
        if (action.control != ActionControl::TapAndHold) {
            return {};
        }

        intent = SelectionIntent::StartCruise;
    }

    const bool cruiseWasActive = m_map.CruiseStateWhenOpened() == ObservedCruiseState::Active;
    return Execute(m_navigation.SelectDestination(*action.destination, intent, cruiseWasActive,
        RemoteSelectionContext {
            .source = {
                .session = identity.session,
                .movieGeneration = identity.generation,
            },
            .inputDevice = environment.inputDevice,
        }, m_map.ShipContextWhenOpened()));
}

EffectDispatchResult CruiseRuntime::OnRemoteRouteCommitted(OperationId operationId, const MapSessionIdentity& identity)
{
    return Execute(m_navigation.RemoteRouteCommitted(operationId, {
        .session = identity.session,
        .movieGeneration = identity.generation,
    }));
}

EffectDispatchResult CruiseRuntime::OnRemoteRouteFailed(OperationId operationId, const MapSessionIdentity& identity)
{
    return Execute(m_navigation.RemoteRouteFailed(operationId, {
        .session = identity.session,
        .movieGeneration = identity.generation,
    }));
}

EffectDispatchResult CruiseRuntime::OnRemoteArrival(RemoteArrivalObservation observation)
{
    return Execute(m_navigation.ObserveRemoteArrival(std::move(observation)));
}

bool CruiseRuntime::OnRemoteOperationCancelled(OperationId operationId)
{
    return m_navigation.CancelRemoteOperation(operationId);
}

bool CruiseRuntime::OnRemoteFlightInvalidated()
{
    return m_navigation.InvalidateRemoteFlight();
}

bool CruiseRuntime::OnLoadGame()
{
    return m_navigation.ResetForLoad();
}

bool CruiseRuntime::OnRemoteCruiseExitTimedOut(OperationId operationId)
{
    return m_navigation.RemoteCruiseExitTimedOut(operationId);
}

EffectDispatchResult CruiseRuntime::OnCruiseChanged(bool active)
{
    return Execute(m_navigation.CruiseChanged(active));
}

bool CruiseRuntime::OnCruiseActivationTimedOut(OperationId operationId)
{
    return m_navigation.RecoverFromEffectFailure(PressCruise {.operationId = operationId});
}

EffectDispatchResult CruiseRuntime::OnCourseLockChanged(FormID lockedCourseId)
{
    return Execute(m_navigation.CourseLockChanged(lockedCourseId));
}

bool CruiseRuntime::OnCourseLockTimedOut(FormID courseId, OperationId operationId)
{
    return m_navigation.RecoverFromEffectFailure(RequestCourse {
        .courseId = courseId,
        .operationId = operationId,
    });
}

const NavigationState& CruiseRuntime::CurrentNavigationState() const
{
    return m_navigation.CurrentState();
}

EffectDispatchResult CruiseRuntime::Execute(TransitionResult transition)
{
    EffectDispatchResult result {
        .handled = transition.handled,
    };

    if (!transition.handled || !transition.effect) {
        return result;
    }

    if (!std::visit(DispatchVisitor {m_commands}, *transition.effect)) {
        result.failedEffect = transition.effect;
        m_navigation.RecoverFromEffectFailure(*transition.effect);
    }

    return result;
}
