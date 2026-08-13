#include "Navigation/NavigationRuntime.h"
#include "TestSuites.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error{ std::string{ message } };
    }

    template <class T>
    const T* FindEffect(const ::TransitionResult& result)
    {
        for (const auto& effect : result.effects) {
            if (const auto* value = std::get_if<T>(&effect))
                return value;
        }

        return nullptr;
    }

    ::Destination Jemison()
    {
        return ::Destination{
            .kind = ::DestinationKind::Planet,
            .targetId = 0x00000010,
            .courseId = 0x00000010,
            .systemId = 0x00000100,
            .displayName = "Jemison",
        };
    }

    ::Destination Mars()
    {
        return ::Destination{
            .kind = ::DestinationKind::Planet,
            .targetId = 0x00000020,
            .courseId = 0x00000020,
            .systemId = 0x00000100,
            .displayName = "Mars",
        };
    }

    void TestTapMarksDestination()
    {
        ::NavigationRuntime runtime;

        const auto selected = runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::Mark,
            false);

        Require(selected.handled,
            "valid destination was not accepted");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::ClosingMap,
            "selection did not wait for map close");
        Require(FindEffect<::CloseMap>(selected) != nullptr,
            "selection did not request map close");

        const auto closed = runtime.MapClosed();

        Require(closed.handled,
            "expected map close was not handled");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Marked,
            "tap did not leave the destination marked");
        Require(runtime.CurrentState().destination.has_value(),
            "tap lost the selected destination");
        Require(runtime.CurrentState().destination->targetId ==
                Jemison().targetId,
            "tap retained the wrong destination");
        Require(closed.effects.empty(),
            "plain tap emitted an unexpected engine effect");
    }

    void TestSameDestinationTogglesOff()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::Mark,
            false);
        runtime.MapClosed();

        const auto toggled = runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::Mark,
            false);

        Require(toggled.handled,
            "same-destination toggle was not handled");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Idle,
            "same-destination toggle did not return to Idle");
        Require(!runtime.CurrentState().destination,
            "same-destination toggle retained the destination");
        Require(FindEffect<::CloseMap>(toggled) != nullptr,
            "same-destination toggle did not request map close");
    }

    void TestDestinationReplacement()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::Mark,
            false);
        runtime.MapClosed();

        runtime.SelectDestination(
            Mars(),
            ::SelectionIntent::Mark,
            false);

        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::ClosingMap,
            "replacement did not enter ClosingMap");
        Require(runtime.CurrentState().destination.has_value(),
            "replacement lost the destination");
        Require(runtime.CurrentState().destination->targetId ==
                Mars().targetId,
            "replacement retained the old destination");

        runtime.MapClosed();

        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Marked,
            "replacement was not marked after map close");
    }

    void TestCompletedHoldUsesExactCourseLock()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::StartCruise,
            false);

        const auto closed = runtime.MapClosed();

        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::CruiseRequested,
            "completed hold did not request Cruise");
        Require(FindEffect<::PressCruise>(closed) != nullptr,
            "completed hold did not emit PressCruise");

        const auto activated = runtime.CruiseChanged(true);
        const auto* course = FindEffect<::RequestCourse>(activated);

        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::AwaitingCourseLock,
            "Cruise activation did not await course confirmation");
        Require(course != nullptr,
            "Cruise activation did not request the course");
        Require(course->courseId == Jemison().courseId,
            "Cruise activation requested the wrong course");

        const auto unrelated = runtime.CourseLockChanged(0xDEADBEEF);

        Require(!unrelated.handled,
            "unrelated course lock was accepted");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::AwaitingCourseLock,
            "unrelated course lock changed navigation phase");

        const auto exact = runtime.CourseLockChanged(
            Jemison().courseId);

        Require(exact.handled,
            "exact course lock was ignored");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::CourseLocked,
            "exact course lock did not establish success");

        const auto lost = runtime.CourseLockChanged(0);

        Require(lost.handled,
            "exact lock loss was ignored");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Marked,
            "lock loss did not return to Marked");
        Require(runtime.CurrentState().destination.has_value(),
            "lock loss discarded the destination");
    }

    void TestAlreadyCruisingSkipsCruisePress()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(
            Mars(),
            ::SelectionIntent::Mark,
            true);

        const auto closed = runtime.MapClosed();
        const auto* course = FindEffect<::RequestCourse>(closed);

        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::AwaitingCourseLock,
            "active-Cruise selection did not await course lock");
        Require(FindEffect<::PressCruise>(closed) == nullptr,
            "active-Cruise selection emitted another Cruise press");
        Require(course != nullptr,
            "active-Cruise selection did not request a course");
        Require(course->courseId == Mars().courseId,
            "active-Cruise selection requested the wrong course");
    }

    void TestInvalidDestinationFailsClosed()
    {
        ::NavigationRuntime runtime;

        const auto result = runtime.SelectDestination(
            ::Destination{},
            ::SelectionIntent::Mark,
            false);

        Require(!result.handled,
            "invalid destination was accepted");
        Require(result.effects.empty(),
            "invalid destination emitted effects");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Idle,
            "invalid destination changed navigation phase");
        Require(!runtime.CurrentState().destination,"invalid destination was retained");
    }

    void TestCloseMapFailureAbandonsIncompleteSelection()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::Mark,
            false);

        const bool recovered = runtime.RecoverFromEffectFailure(
            ::CloseMap{});

        Require(recovered,
            "CloseMap failure was not recovered");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Idle,
            "CloseMap failure left navigation waiting for map close");
        Require(!runtime.CurrentState().destination,
            "CloseMap failure retained an incomplete selection");
        Require(!runtime.MapClosed().handled,
            "CloseMap failure retained pending map-close state");
    }

    void TestCruisePressFailureFallsBackToMark()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::StartCruise,
            false);
        runtime.MapClosed();

        const bool recovered = runtime.RecoverFromEffectFailure(
            ::PressCruise{});

        Require(recovered,
            "PressCruise failure was not recovered");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Marked,
            "PressCruise failure did not fall back to a mark");
        Require(runtime.CurrentState().destination.has_value(),
            "PressCruise failure discarded the destination");
        Require(runtime.CurrentState().destination->targetId ==
                Jemison().targetId,
            "PressCruise failure retained the wrong destination");
    }

    void TestOnlyExactCourseRequestFailureRecovers()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::Mark,
            false);
        runtime.MapClosed();
        runtime.CruiseChanged(true);

        const bool staleRecovered = runtime.RecoverFromEffectFailure(
            ::RequestCourse{ Mars().courseId });

        Require(!staleRecovered,
            "unrelated RequestCourse failure changed navigation");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::AwaitingCourseLock,
            "unrelated RequestCourse failure left the expected phase");

        const bool exactRecovered = runtime.RecoverFromEffectFailure(
            ::RequestCourse{ Jemison().courseId });

        Require(exactRecovered,
            "exact RequestCourse failure was not recovered");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Marked,
            "RequestCourse failure did not fall back to a mark");
        Require(runtime.CurrentState().destination.has_value(),
            "RequestCourse failure discarded the destination");
    }

    void TestFailureOutsideOwningPhaseIsIgnored()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(
            Jemison(),
            ::SelectionIntent::Mark,
            false);
        runtime.MapClosed();

        const bool recovered = runtime.RecoverFromEffectFailure(
            ::PressCruise{});

        Require(!recovered,
            "stale PressCruise failure was accepted");
        Require(runtime.CurrentState().phase ==
                ::NavigationPhase::Marked,
            "stale failure changed navigation phase");
        Require(runtime.CurrentState().destination.has_value(),
            "stale failure discarded the destination");
    }

    void RunTests()
    {
        TestTapMarksDestination();
        TestSameDestinationTogglesOff();
        TestDestinationReplacement();
        TestCompletedHoldUsesExactCourseLock();
        TestAlreadyCruisingSkipsCruisePress();
        TestInvalidDestinationFailsClosed();
        TestCloseMapFailureAbandonsIncompleteSelection();
        TestCruisePressFailureFallsBackToMark();
        TestOnlyExactCourseRequestFailureRecovers();
        TestFailureOutsideOwningPhaseIsIgnored();
    }
}

void RunNavigationRuntimeTests()
{
    RunTests();
}
