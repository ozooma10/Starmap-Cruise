#pragma once

#include "Application/BodyResolver.h"
#include "Application/CruiseRuntime.h"
#include "Application/EffectDispatcher.h"

class CruiseController
{
public:
    CruiseController(const BodyResolutionSource& bodySource, CruiseCommands& commands);

    void OnMapMovieCreated(std::uint32_t generation);

    bool OnMapOpened(const MapOpenContext& context);

    EffectDispatchResult OnMapClosed(const MapSessionIdentity& identity);

    bool OnMapViewChanged(const MapSessionIdentity& identity, MapView view);

    bool OnMarkersChanged(const MapSessionIdentity& identity, MarkerUpdate update);

    bool OnDossierChanged(const MapSessionIdentity& identity, const TargetObservation& dossier);

    // Re-evaluates the current dossier when the asynchronous body index becomes ready.
    bool RefreshBodyResolution(const MapSessionIdentity& identity, const TargetObservation& dossier);

    bool OnCurrentSystemResolved(const MapSessionIdentity& identity, FormID systemId);

    ActionDecision CurrentMapAction(const MapActionEnvironment& environment) const;

    EffectDispatchResult ActivateMapAction(const MapSessionIdentity& identity, MapActionGesture gesture, const MapActionEnvironment& environment);

    EffectDispatchResult OnCruiseChanged(bool active);

    EffectDispatchResult OnCourseLockChanged(FormID lockedCourseId);

    const NavigationState& CurrentNavigationState() const;

private:
    EffectDispatchResult Execute(TransitionResult transition);

    CruiseRuntime runtime_;
    BodyResolver bodyResolver_;
    CruiseCommands& commands_;
};