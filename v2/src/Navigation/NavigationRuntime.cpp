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
    if (m_state.destination && m_state.destination->SameIdentityAs(destination)) {
        Reset();
        result.effect = CloseMap {};
        return result;
    }

    m_state.destination = std::move(destination);
    m_state.phase = NavigationPhase::ClosingMap;

    m_pendingIntent = intent;
    m_cruiseWasActiveWhenSelected = cruiseAlreadyActive;

    result.effect = CloseMap {};
    return result;
}

TransitionResult NavigationRuntime::MapClosed()
{
    TransitionResult result;

    if (m_state.phase != NavigationPhase::ClosingMap || !m_state.destination || !m_pendingIntent) {
        return result;
    }

    result.handled = true;

    const auto intent = *m_pendingIntent;
    const bool cruiseWasActive = m_cruiseWasActiveWhenSelected;

    m_pendingIntent.reset();
    m_cruiseWasActiveWhenSelected = false;

    if (cruiseWasActive) {
        m_state.phase = NavigationPhase::AwaitingCourseLock;
        result.effect = RequestCourse {m_state.destination->courseId};
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

TransitionResult NavigationRuntime::CruiseChanged(bool active)
{
    TransitionResult result;

    if (!m_state.destination) {
        return result;
    }

    if (active && (m_state.phase == NavigationPhase::Marked || m_state.phase == NavigationPhase::CruiseRequested)) {
        m_state.phase = NavigationPhase::AwaitingCourseLock;

        result.handled = true;
        result.effect = RequestCourse {m_state.destination->courseId};
        return result;
    }

    if (!active && (m_state.phase == NavigationPhase::AwaitingCourseLock || m_state.phase == NavigationPhase::CourseLocked)) {
        // Cruise ending does not discard the players mark.
        m_state.phase = NavigationPhase::Marked;
        result.handled = true;
    }

    return result;
}

TransitionResult NavigationRuntime::CourseLockChanged(FormID lockedCourseId)
{
    TransitionResult result;

    if (!m_state.destination) {
        return result;
    }

    const auto expectedCourseId = m_state.destination->courseId;

    if (lockedCourseId == expectedCourseId && (m_state.phase == NavigationPhase::Marked || m_state.phase == NavigationPhase::AwaitingCourseLock || m_state.phase == NavigationPhase::CourseLocked)) {
        m_state.phase = NavigationPhase::CourseLocked;
        result.handled = true;
        return result;
    }

    if (m_state.phase == NavigationPhase::CourseLocked && lockedCourseId != expectedCourseId) {
        // The exact course disappeared or another course replaced it.
        // Keep the destination marked. Arrival handling comes later.
        m_state.phase = NavigationPhase::Marked;
        result.handled = true;
    }

    return result;
}

bool NavigationRuntime::InvalidateMapSelection()
{
    if (m_state.phase != NavigationPhase::ClosingMap) {
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

    if (std::holds_alternative<PressCruise>(effect)) {
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
