#include "Navigation/NavigationRuntime.h"

#include <utility>

TransitionResult NavigationRuntime::SelectDestination(Destination destination, SelectionIntent intent, bool cruiseAlreadyActive)
{
    TransitionResult result;

    if (!destination.IsValid()) {
        return result;
    }

    result.handled = true;

    // Selecting the currently marked destination acts as a toggle.
    if (state_.destination && state_.destination->SameIdentityAs(destination)) {
        Reset();
        result.effect = CloseMap {};
        return result;
    }

    state_.destination = std::move(destination);
    state_.phase = NavigationPhase::ClosingMap;

    pendingIntent_ = intent;
    cruiseWasActiveWhenSelected_ = cruiseAlreadyActive;

    result.effect = CloseMap {};
    return result;
}

TransitionResult NavigationRuntime::MapClosed()
{
    TransitionResult result;

    if (state_.phase != NavigationPhase::ClosingMap || !state_.destination || !pendingIntent_) {
        return result;
    }

    result.handled = true;

    const auto intent = *pendingIntent_;
    const bool cruiseWasActive = cruiseWasActiveWhenSelected_;

    pendingIntent_.reset();
    cruiseWasActiveWhenSelected_ = false;

    if (cruiseWasActive) {
        state_.phase = NavigationPhase::AwaitingCourseLock;
        result.effect = RequestCourse {state_.destination->courseId};
        return result;
    }

    if (intent == SelectionIntent::Mark) {
        state_.phase = NavigationPhase::Marked;
        return result;
    }

    state_.phase = NavigationPhase::CruiseRequested;
    result.effect = PressCruise {};
    return result;
}

TransitionResult NavigationRuntime::CruiseChanged(bool active)
{
    TransitionResult result;

    if (!state_.destination) {
        return result;
    }

    if (active && (state_.phase == NavigationPhase::Marked || state_.phase == NavigationPhase::CruiseRequested)) {
        state_.phase = NavigationPhase::AwaitingCourseLock;

        result.handled = true;
        result.effect = RequestCourse {state_.destination->courseId};
        return result;
    }

    if (!active && (state_.phase == NavigationPhase::AwaitingCourseLock || state_.phase == NavigationPhase::CourseLocked)) {
        // Cruise ending does not discard the players mark.
        state_.phase = NavigationPhase::Marked;
        result.handled = true;
    }

    return result;
}

TransitionResult NavigationRuntime::CourseLockChanged(FormID lockedCourseId)
{
    TransitionResult result;

    if (!state_.destination) {
        return result;
    }

    const auto expectedCourseId = state_.destination->courseId;

    if (lockedCourseId == expectedCourseId && (state_.phase == NavigationPhase::Marked || state_.phase == NavigationPhase::AwaitingCourseLock || state_.phase == NavigationPhase::CourseLocked)) {
        state_.phase = NavigationPhase::CourseLocked;
        result.handled = true;
        return result;
    }

    if (state_.phase == NavigationPhase::CourseLocked && lockedCourseId != expectedCourseId) {
        // The exact course disappeared or another course replaced it.
        // Keep the destination marked. Arrival handling comes later.
        state_.phase = NavigationPhase::Marked;
        result.handled = true;
    }

    return result;
}

bool NavigationRuntime::RecoverFromEffectFailure(const Effect& effect)
{
    if (std::holds_alternative<CloseMap>(effect)) {
        if (state_.phase != NavigationPhase::ClosingMap) {
            return false;
        }

        Reset();
        return true;
    }

    if (std::holds_alternative<PressCruise>(effect)) {
        if (state_.phase != NavigationPhase::CruiseRequested || !state_.destination) {
            return false;
        }

        state_.phase = NavigationPhase::Marked;
        return true;
    }

    const auto* request = std::get_if<RequestCourse>(&effect);
    if (!request || state_.phase != NavigationPhase::AwaitingCourseLock || !state_.destination || request->courseId != state_.destination->courseId) {
        return false;
    }

    state_.phase = NavigationPhase::Marked;
    return true;
}

void NavigationRuntime::Reset()
{
    state_ = {};
    pendingIntent_.reset();
    cruiseWasActiveWhenSelected_ = false;
}

const NavigationState& NavigationRuntime::CurrentState() const
{
    return state_;
}
