#include "Application/CruiseController.h"

#include <utility>

CruiseController::CruiseController(const BodyResolutionSource& bodySource, CruiseCommands& commands) :
    bodyResolver_(bodySource), commands_(commands) {}

void CruiseController::OnMapMovieCreated(std::uint32_t generation)
{
    runtime_.OnMapMovieCreated(generation);
}

bool CruiseController::OnMapOpened(const MapOpenContext& context)
{
    return runtime_.OnMapOpened(context);
}

EffectDispatchResult CruiseController::OnMapClosed(const MapSessionIdentity& identity)
{
    return Execute(runtime_.OnMapClosed(identity));
}

bool CruiseController::OnMapViewChanged(const MapSessionIdentity& identity, MapView view)
{
    return runtime_.OnMapViewChanged(identity, view);
}

bool CruiseController::OnMarkersChanged(const MapSessionIdentity& identity, MarkerUpdate update)
{
    return runtime_.OnMarkersChanged(identity, std::move(update));
}

bool CruiseController::OnDossierChanged(const MapSessionIdentity& identity, const TargetObservation& dossier)
{
    if (!runtime_.OnDossierChanged(identity, dossier)) {
        return false;
    }

    // An empty dossier legitimately clears the current observation.
    if (dossier.id == 0) {
        return true;
    }

    return RefreshBodyResolution(identity, dossier);
}

bool CruiseController::RefreshBodyResolution(const MapSessionIdentity& identity, const TargetObservation& dossier)
{
    if (dossier.id == 0) {
        return false;
    }

    return runtime_.OnBodyResolved(identity, bodyResolver_.Resolve(dossier));
}

bool CruiseController::OnCurrentSystemResolved(const MapSessionIdentity& identity, FormID systemId)
{
    return runtime_.OnCurrentSystemResolved(identity, systemId);
}

ActionDecision CruiseController::CurrentMapAction(const MapActionEnvironment& environment) const
{
    return runtime_.CurrentMapAction(environment);
}

EffectDispatchResult CruiseController::ActivateMapAction(const MapSessionIdentity& identity, MapActionGesture gesture, const MapActionEnvironment& environment)
{
    return Execute(runtime_.ActivateMapAction(identity, gesture, environment));
}

EffectDispatchResult CruiseController::OnCruiseChanged(bool active)
{
    return Execute(runtime_.OnCruiseChanged(active));
}

EffectDispatchResult CruiseController::OnCourseLockChanged(FormID lockedCourseId)
{
    return Execute(runtime_.OnCourseLockChanged(lockedCourseId));
}

const NavigationState&
CruiseController::CurrentNavigationState() const
{
    return runtime_.CurrentNavigationState();
}

EffectDispatchResult CruiseController::Execute(TransitionResult transition)
{
    auto result = DispatchEffects(transition, commands_);

    if (result.failedEffect) {
        runtime_.RecoverFromEffectFailure(*result.failedEffect);
    }

    return result;
}
