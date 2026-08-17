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
    virtual bool PressCruise() = 0;
    virtual bool RequestCourse(FormID courseId) = 0;
};

struct EffectDispatchResult
{
    bool handled {false};
    std::optional<Effect> failedEffect;

    bool Succeeded() const
    {
        return handled && !failedEffect.has_value();
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

    SelectionDecision CurrentSelection() const;
    ActionDecision CurrentMapAction(const MapActionEnvironment& environment) const;

    EffectDispatchResult ActivateMapAction(const MapSessionIdentity& identity, MapActionGesture gesture, const MapActionEnvironment& environment);

    EffectDispatchResult OnCruiseChanged(bool active);
    bool OnCruiseActivationTimedOut();

    EffectDispatchResult OnCourseLockChanged(FormID lockedCourseId);
    bool OnCourseLockTimedOut(FormID courseId);

    const NavigationState& CurrentNavigationState() const;

private:
    EffectDispatchResult Execute(TransitionResult transition);

    MapSessionState m_map;
    NavigationRuntime m_navigation;
    const BodyResolutionSource& m_bodySource;
    CruiseCommands& m_commands;
};
