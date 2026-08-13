#pragma once

#include "Map/MapSessionState.h"
#include "Navigation/NavigationRuntime.h"
#include "Presentation/ActionPolicy.h"

#include <cstdint>

enum class MapActionGesture : std::uint8_t
{
    Tap,
    HoldCompleted,
};

struct MapActionEnvironment
{
    bool cruiseControlBound{ false };
    bool cruiseEngageAvailable{ false };
    bool vanillaActionEnabled{ false };
};

class CruiseRuntime
{
public:
    void OnMapMovieCreated(std::uint32_t generation);

    bool OnMapOpened(const MapOpenContext& context);
    TransitionResult OnMapClosed(const MapSessionIdentity& identity);

    bool OnMapViewChanged(const MapSessionIdentity& identity, MapView view);

    bool OnMarkersChanged(const MapSessionIdentity& identity, MarkerUpdate update);

    bool OnDossierChanged(const MapSessionIdentity& identity, TargetObservation dossier);

    bool OnBodyResolved(const MapSessionIdentity& identity, BodyResolutionUpdate resolution);

    bool OnCurrentSystemResolved(const MapSessionIdentity& identity, FormID systemId);

    ActionDecision CurrentMapAction(const MapActionEnvironment& environment) const;

    TransitionResult ActivateMapAction(const MapSessionIdentity& identity, MapActionGesture gesture, const MapActionEnvironment& environment);

    TransitionResult OnCruiseChanged(bool active);

    TransitionResult OnCourseLockChanged(FormID lockedCourseId);

    const NavigationState& CurrentNavigationState() const;

private:
    MapSessionState map_;
    NavigationRuntime navigation_;
};