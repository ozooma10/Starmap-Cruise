#include "Application/CruiseRuntime.h"

#include <utility>

void CruiseRuntime::OnMapMovieCreated(std::uint32_t generation)
{
    map_.BeginMovie(generation);
}

bool CruiseRuntime::OnMapOpened(const MapOpenContext& context)
{
    return map_.Open(context);
}

TransitionResult CruiseRuntime::OnMapClosed(const MapSessionIdentity& identity)
{
    if (!map_.Close(identity)) {
        return {};
    }

    return navigation_.MapClosed();
}

bool CruiseRuntime::OnMapViewChanged(const MapSessionIdentity& identity, MapView view)
{
    return map_.SetView(identity, view);
}

bool CruiseRuntime::OnMarkersChanged(const MapSessionIdentity& identity, MarkerUpdate update)
{
    return map_.SetMarkers(identity, std::move(update));
}

bool CruiseRuntime::OnDossierChanged(const MapSessionIdentity& identity, TargetObservation dossier)
{
    return map_.SetDossier(identity, std::move(dossier));
}

bool CruiseRuntime::OnBodyResolved(const MapSessionIdentity& identity, BodyResolutionUpdate resolution)
{
    return map_.SetBodyResolution(identity, std::move(resolution));
}

bool CruiseRuntime::OnCurrentSystemResolved(const MapSessionIdentity& identity, FormID systemId)
{
    return map_.CaptureCurrentSystem(identity, systemId);
}

ActionDecision CruiseRuntime::CurrentMapAction(const MapActionEnvironment& environment) const
{
    const auto selection =
        EvaluateSelection(map_.Snapshot());

    const ActionContext context{
        .cruiseControlBound = environment.cruiseControlBound,
        .cruiseWasActiveWhenMapOpened = map_.CruiseWasActiveWhenOpened(),
        .cruiseEngageAvailable = environment.cruiseEngageAvailable,
        .vanillaActionEnabled = environment.vanillaActionEnabled,
    };

    return EvaluateAction(selection, context);
}

TransitionResult CruiseRuntime::ActivateMapAction(const MapSessionIdentity& identity, MapActionGesture gesture, const MapActionEnvironment& environment)
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

    return navigation_.SelectDestination(
        *action.destination,
        intent,
        map_.CruiseWasActiveWhenOpened());
}

TransitionResult CruiseRuntime::OnCruiseChanged(bool active)
{
    return navigation_.CruiseChanged(active);
}

TransitionResult CruiseRuntime::OnCourseLockChanged(FormID lockedCourseId)
{
    return navigation_.CourseLockChanged(lockedCourseId);
}

const NavigationState& CruiseRuntime::CurrentNavigationState() const
{
    return navigation_.CurrentState();
}