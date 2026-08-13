#include "Application/CruiseRuntime.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace
{
    constexpr ::MapSessionIdentity CurrentIdentity{
        .session = 7,
        .generation = 3,
    };

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

    ::MapActionEnvironment ReadyEnvironment()
    {
        return {
            .cruiseControlBound = true,
            .cruiseEngageAvailable = true,
            .vanillaActionEnabled = true,
        };
    }

    void OpenEligibleMap(::CruiseRuntime& runtime, bool cruiseWasActive = false)
    {
        runtime.OnMapMovieCreated(CurrentIdentity.generation);

        Require(runtime.OnMapOpened({
            .identity = CurrentIdentity,
            .flying = true,
            .cruiseWasActive = cruiseWasActive,
            .currentSystemId = 0x100,
        }), "map session was not opened");

        Require(runtime.OnMapViewChanged(CurrentIdentity, ::MapView::System),
            "system view was rejected");

        Require(runtime.OnMarkersChanged(CurrentIdentity, {
            .highlightedCount = 1,
            .highlighted = {
                .id = 0x10,
                .kind = ::ObservedTargetKind::Planet,
                .displayName = "Jemison Marker",
            },
        }), "marker update was rejected");

        Require(runtime.OnDossierChanged(CurrentIdentity, {
            .id = 0x10,
            .kind = ::ObservedTargetKind::Planet,
            .displayName = "Jemison",
        }), "dossier update was rejected");

        Require(runtime.OnBodyResolved(CurrentIdentity, {
            .dossierId = 0x10,
            .dossierIsLiveBody = true,
            .bodyIndexReady = true,
            .indexedBody = ::IndexedBodyObservation{
                .id = 0x10,
                .systemId = 0x100,
            },
        }), "body resolution was rejected");
    }

    void TestEligibleSessionProducesAction()
    {
        ::CruiseRuntime runtime;
        OpenEligibleMap(runtime);

        const auto action = runtime.CurrentMapAction(ReadyEnvironment());

        Require(action.CanHandleInput(),
            "eligible session did not produce an actionable decision");
        Require(action.control == ::ActionControl::TapAndHold,
            "available cruise did not expose tap-and-hold control");
        Require(action.destination.has_value() && action.destination->targetId == 0x10,
            "action did not retain the resolved destination");
    }

    void TestTapFlowsIntoNavigation()
    {
        ::CruiseRuntime runtime;
        OpenEligibleMap(runtime);

        const auto activated = runtime.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::Tap,
            ReadyEnvironment());

        Require(activated.handled, "tap was not handled");
        Require(FindEffect<::CloseMap>(activated) != nullptr,
            "tap did not request that the map close");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::ClosingMap,
            "tap did not enter the closing-map phase");

        const auto closed = runtime.OnMapClosed(CurrentIdentity);

        Require(closed.handled, "accepted map close was not handled");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Marked,
            "mark intent did not finish after the map closed");
    }

    void TestHoldReachesExactCourseLock()
    {
        ::CruiseRuntime runtime;
        OpenEligibleMap(runtime);

        const auto activated = runtime.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::HoldCompleted,
            ReadyEnvironment());

        Require(activated.handled, "completed hold was not handled");
        Require(FindEffect<::CloseMap>(activated) != nullptr,
            "completed hold did not request that the map close");

        const auto closed = runtime.OnMapClosed(CurrentIdentity);
        Require(FindEffect<::PressCruise>(closed) != nullptr,
            "map close did not request cruise engagement");

        const auto cruiseStarted = runtime.OnCruiseChanged(true);
        const auto* requestCourse = FindEffect<::RequestCourse>(cruiseStarted);
        Require(requestCourse != nullptr,
            "cruise engagement did not request a course");
        Require(requestCourse->courseId == 0x10,
            "course request targeted the wrong destination");

        runtime.OnCourseLockChanged(0x20);
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::AwaitingCourseLock,
            "an unrelated course lock advanced navigation");

        runtime.OnCourseLockChanged(0x10);
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::CourseLocked,
            "the requested course lock did not finish navigation");
    }

    void TestTapOnlyRejectsHold()
    {
        ::CruiseRuntime runtime;
        OpenEligibleMap(runtime);

        auto environment = ReadyEnvironment();
        environment.cruiseEngageAvailable = false;

        const auto action = runtime.CurrentMapAction(environment);
        Require(action.control == ::ActionControl::TapOnly,
            "unavailable cruise did not reduce the action to tap-only");

        const auto held = runtime.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::HoldCompleted,
            environment);

        Require(!held.handled, "tap-only action accepted a hold");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle,
            "rejected hold changed navigation state");

        const auto tapped = runtime.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::Tap,
            environment);
        Require(tapped.handled, "tap-only action rejected a tap");
    }

    void TestAlreadyCruisingTapRequestsCourseOnClose()
    {
        ::CruiseRuntime runtime;
        OpenEligibleMap(runtime, true);

        const auto action = runtime.CurrentMapAction(ReadyEnvironment());
        Require(action.control == ::ActionControl::TapOnly,
            "active cruise did not reduce the action to tap-only");

        const auto activated = runtime.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::Tap,
            ReadyEnvironment());
        Require(activated.handled, "tap while cruising was not handled");

        const auto closed = runtime.OnMapClosed(CurrentIdentity);
        const auto* requestCourse = FindEffect<::RequestCourse>(closed);

        Require(requestCourse != nullptr,
            "active cruise did not request the course directly after close");
        Require(requestCourse->courseId == 0x10,
            "direct course request targeted the wrong destination");
        Require(FindEffect<::PressCruise>(closed) == nullptr,
            "active cruise incorrectly requested another cruise press");
    }

    void TestStaleSessionCannotActivateAction()
    {
        ::CruiseRuntime runtime;
        OpenEligibleMap(runtime);

        const ::MapSessionIdentity staleIdentity{
            .session = CurrentIdentity.session - 1,
            .generation = CurrentIdentity.generation,
        };

        const auto activated = runtime.ActivateMapAction(
            staleIdentity,
            ::MapActionGesture::Tap,
            ReadyEnvironment());

        Require(!activated.handled, "stale session activated the current action");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle,
            "stale activation changed navigation state");
    }

    void TestEffectFailureRecoveryIsExposedByApplicationRuntime()
    {
        ::CruiseRuntime runtime;
        OpenEligibleMap(runtime);

        runtime.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::HoldCompleted,
            ReadyEnvironment());
        runtime.OnMapClosed(CurrentIdentity);

        const bool recovered = runtime.RecoverFromEffectFailure(
            ::PressCruise{});

        Require(recovered,
            "application runtime did not forward effect failure recovery");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Marked,
            "application runtime did not expose the recovered navigation state");
        Require(runtime.CurrentNavigationState().destination.has_value(),
            "application recovery discarded the selected destination");
    }

    void RunTests()
    {
        TestEligibleSessionProducesAction();
        TestTapFlowsIntoNavigation();
        TestHoldReachesExactCourseLock();
        TestTapOnlyRejectsHold();
        TestAlreadyCruisingTapRequestsCourseOnClose();
        TestStaleSessionCannotActivateAction();
        TestEffectFailureRecoveryIsExposedByApplicationRuntime();
    }
}

void RunCruiseRuntimeTests()
{
    RunTests();
}
