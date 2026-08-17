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

        bool operator()(const ::PressCruise&) const
        {
            return commands.PressCruise();
        }

        bool operator()(const ::RequestCourse& effect) const
        {
            return commands.RequestCourse(effect.courseId);
        }
    };
}

CruiseRuntime::CruiseRuntime(const BodyResolutionSource& bodySource, CruiseCommands& commands) : bodySource_(bodySource), commands_(commands) {}

void CruiseRuntime::OnMapMovieCreated(std::uint32_t generation)
{
    map_.BeginMovie(generation);
}

bool CruiseRuntime::OnMapOpened(const MapOpenContext& context)
{
    return map_.Open(context);
}

EffectDispatchResult CruiseRuntime::OnMapClosed(const MapSessionIdentity& identity)
{
    if (!map_.Close(identity)) {
        return {};
    }

    return Execute(navigation_.MapClosed());
}

bool CruiseRuntime::OnMapCloseTimedOut(const MapSessionIdentity& identity)
{
    if (!map_.IsActive(identity)) {
        return false;
    }

    return navigation_.RecoverFromEffectFailure(CloseMap {});
}

bool CruiseRuntime::OnMapViewChanged(const MapSessionIdentity& identity, MapView view)
{
    return map_.SetView(identity, view);
}

bool CruiseRuntime::OnMarkersChanged(const MapSessionIdentity& identity, MarkerUpdate update)
{
    return map_.SetMarkers(identity, std::move(update));
}

bool CruiseRuntime::OnDossierChanged(const MapSessionIdentity& identity, const TargetObservation& dossier)
{
    if (!map_.IsActive(identity)) {
        return false;
    }

    std::optional<ResolvedBody> resolvedBody;

    if (dossier.id != 0 && IsSupported(dossier.kind)) {
        resolvedBody = bodySource_.ResolveBody(dossier.id);
    }

    return map_.SetDossier(identity, dossier, std::move(resolvedBody));
}

bool CruiseRuntime::OnCurrentSystemResolved(const MapSessionIdentity& identity, FormID systemId)
{
    return map_.CaptureCurrentSystem(identity, systemId);
}

SelectionDecision CruiseRuntime::CurrentSelection() const
{
    return EvaluateSelection(map_.Snapshot());
}

ActionDecision CruiseRuntime::CurrentMapAction(const MapActionEnvironment& environment) const
{
    const ActionContext context {
        .cruiseControlBound = environment.cruiseControlBound,
        .cruiseStateWhenMapOpened = map_.CruiseStateWhenOpened(),
        .cruiseEngageAvailable = environment.cruiseEngageAvailable,
        .vanillaActionEnabled = environment.vanillaActionEnabled,
    };

    return EvaluateAction(CurrentSelection(), context);
}

EffectDispatchResult CruiseRuntime::ActivateMapAction(const MapSessionIdentity& identity, MapActionGesture gesture, const MapActionEnvironment& environment)
{
    if (!map_.IsActive(identity)) {
        return {};
    }

    const auto action = CurrentMapAction(environment);

    if (!action.CanHandleInput()) {
        return {};
    }

    SelectionIntent intent = SelectionIntent::Mark;

    if (gesture == MapActionGesture::HoldCompleted) {
        if (action.control != ActionControl::TapAndHold) {
            return {};
        }

        intent = SelectionIntent::StartCruise;
    }

    const bool cruiseWasActive = map_.CruiseStateWhenOpened() == ObservedCruiseState::Active;
    return Execute(navigation_.SelectDestination(*action.destination, intent, cruiseWasActive));
}

EffectDispatchResult CruiseRuntime::OnCruiseChanged(bool active)
{
    return Execute(navigation_.CruiseChanged(active));
}

bool CruiseRuntime::OnCruiseActivationTimedOut()
{
    return navigation_.RecoverFromEffectFailure(PressCruise {});
}

EffectDispatchResult CruiseRuntime::OnCourseLockChanged(FormID lockedCourseId)
{
    return Execute(navigation_.CourseLockChanged(lockedCourseId));
}

bool CruiseRuntime::OnCourseLockTimedOut(FormID courseId)
{
    return navigation_.RecoverFromEffectFailure(RequestCourse {courseId});
}

const NavigationState& CruiseRuntime::CurrentNavigationState() const
{
    return navigation_.CurrentState();
}

EffectDispatchResult CruiseRuntime::Execute(TransitionResult transition)
{
    EffectDispatchResult result {
        .handled = transition.handled,
    };

    if (!transition.handled || !transition.effect) {
        return result;
    }

    if (!std::visit(DispatchVisitor {commands_}, *transition.effect)) {
        result.failedEffect = transition.effect;
        navigation_.RecoverFromEffectFailure(*transition.effect);
    }

    return result;
}
