#pragma once

#include "Domain/Destination.h"

#include <optional>
#include <variant>

enum class NavigationPhase : std::uint8_t
{
    Idle,
    ClosingMap,         // A selection was accepted and the map-close effect was emitted.
    Marked,             // A destination exists, but Cruise does not own its course.
    CruiseRequested,    // A completed Starmap hold requested vanilla Cruise activation.
    AwaitingCourseLock, // Cruise is active and the exact course has been requested.
    CourseLocked,       // The HUD confirmed the exact courseId.
};

enum class SelectionIntent : std::uint8_t
{
    Mark,
    StartCruise,
};

struct NavigationState
{
    NavigationPhase phase {NavigationPhase::Idle};
    std::optional<Destination> destination;
};

struct CloseMap
{};

struct PressCruise
{};

struct RequestCourse
{
    FormID courseId {0};
};

using Effect = std::variant<CloseMap, PressCruise, RequestCourse>;

struct TransitionResult
{
    bool handled {false};
    std::optional<Effect> effect;
};

class NavigationRuntime
{
public:
    TransitionResult SelectDestination(Destination destination, SelectionIntent intent, bool cruiseAlreadyActive);
    TransitionResult MapClosed();
    TransitionResult CruiseChanged(bool active);
    TransitionResult CourseLockChanged(FormID lockedCourseId);

    bool RecoverFromEffectFailure(const Effect& effect);

    void Reset();
    const NavigationState& CurrentState() const;

private:
    NavigationState state_;

    std::optional<SelectionIntent> pendingIntent_;
    bool cruiseWasActiveWhenSelected_ {false};
};
