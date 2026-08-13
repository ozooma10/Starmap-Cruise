#include "Application/CruiseController.h"
#include "TestSuites.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    constexpr ::FormID JemisonId = 0x10;
    constexpr ::FormID MarsId = 0x20;
    constexpr ::FormID AlphaCentauriId = 0x100;

    constexpr ::MapSessionIdentity CurrentIdentity{
        .session = 7,
        .generation = 3,
    };

    enum class RecordedCommand
    {
        CloseMap,
        PressCruise,
        RequestCourse,
    };

    struct RecordedCall
    {
        RecordedCommand command;
        ::FormID courseId{ 0 };
    };

    class FakeBodyResolutionSource final : public ::BodyResolutionSource
    {
    public:
        bool IsLiveBody(::FormID bodyId) const override
        {
            ++liveBodyCalls;
            lastBodyId = bodyId;
            return liveBody;
        }

        bool IsBodyIndexReady() const override
        {
            ++indexReadyCalls;
            return indexReady;
        }

        std::optional<::IndexedBodyObservation> FindIndexedBody(
            ::FormID bodyId) const override
        {
            ++findBodyCalls;
            lastBodyId = bodyId;
            return indexedBody;
        }

        bool liveBody{ true };
        bool indexReady{ true };
        std::optional<::IndexedBodyObservation> indexedBody{
            ::IndexedBodyObservation{
                .id = JemisonId,
                .systemId = AlphaCentauriId,
            }
        };

        mutable std::size_t liveBodyCalls{ 0 };
        mutable std::size_t indexReadyCalls{ 0 };
        mutable std::size_t findBodyCalls{ 0 };
        mutable ::FormID lastBodyId{ 0 };
    };

    class FakeCruiseCommands final : public ::CruiseCommands
    {
    public:
        bool CloseMap() override
        {
            return Record(RecordedCommand::CloseMap);
        }

        bool PressCruise() override
        {
            return Record(RecordedCommand::PressCruise);
        }

        bool RequestCourse(::FormID courseId) override
        {
            return Record(RecordedCommand::RequestCourse, courseId);
        }

        std::optional<RecordedCommand> failOn;
        std::vector<RecordedCall> calls;

    private:
        bool Record(RecordedCommand command, ::FormID courseId = 0)
        {
            calls.push_back({
                .command = command,
                .courseId = courseId,
            });

            return !failOn || *failOn != command;
        }
    };

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error{ std::string{ message } };
    }

    ::TargetObservation JemisonDossier()
    {
        return {
            .id = JemisonId,
            .kind = ::ObservedTargetKind::Planet,
            .displayName = "Jemison",
        };
    }

    ::MapActionEnvironment ReadyEnvironment()
    {
        return {
            .cruiseControlBound = true,
            .cruiseEngageAvailable = true,
            .vanillaActionEnabled = true,
        };
    }

    void OpenMap(
        ::CruiseController& controller,
        bool cruiseWasActive = false)
    {
        controller.OnMapMovieCreated(CurrentIdentity.generation);

        Require(controller.OnMapOpened({
            .identity = CurrentIdentity,
            .flying = true,
            .cruiseWasActive = cruiseWasActive,
            .currentSystemId = AlphaCentauriId,
        }), "controller rejected the map session");

        Require(controller.OnMapViewChanged(
            CurrentIdentity,
            ::MapView::System),
            "controller rejected system view");

        Require(controller.OnMarkersChanged(CurrentIdentity, {
            .highlightedCount = 1,
            .highlighted = {
                .id = JemisonId,
                .kind = ::ObservedTargetKind::Planet,
                .displayName = "Jemison Marker",
            },
        }), "controller rejected marker observation");

        Require(controller.OnDossierChanged(
            CurrentIdentity,
            JemisonDossier()),
            "controller rejected dossier observation");
    }

    void TestFullTapFlow()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseController controller{ bodySource, commands };
        OpenMap(controller);

        const auto action = controller.CurrentMapAction(
            ReadyEnvironment());

        Require(action.CanHandleInput(),
            "automatically resolved dossier did not produce an action");
        Require(bodySource.liveBodyCalls == 1 &&
                bodySource.indexReadyCalls == 1 &&
                bodySource.findBodyCalls == 1,
            "dossier did not run through the complete body resolver");
        Require(bodySource.lastBodyId == JemisonId,
            "body resolver received the wrong dossier identity");

        const auto activated = controller.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::Tap,
            ReadyEnvironment());

        Require(activated.Succeeded(),
            "tap command dispatch did not succeed");
        Require(commands.calls.size() == 1 &&
                commands.calls[0].command == RecordedCommand::CloseMap,
            "tap did not dispatch exactly one CloseMap command");
        Require(controller.CurrentNavigationState().phase ==
                ::NavigationPhase::ClosingMap,
            "tap did not wait for the map-close observation");

        const auto closed = controller.OnMapClosed(CurrentIdentity);

        Require(closed.Succeeded(),
            "tap map-close observation was not handled");
        Require(closed.completedCount == 0,
            "plain mark dispatched a command after map close");
        Require(commands.calls.size() == 1,
            "plain mark issued an unexpected second command");
        Require(controller.CurrentNavigationState().phase ==
                ::NavigationPhase::Marked,
            "tap did not finish as a retained mark");
    }

    void TestFullHoldFlow()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseController controller{ bodySource, commands };
        OpenMap(controller);

        Require(controller.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::HoldCompleted,
            ReadyEnvironment()).Succeeded(),
            "completed hold did not close the map");

        Require(controller.OnMapClosed(CurrentIdentity).Succeeded(),
            "map close did not dispatch Cruise activation");

        Require(controller.OnCruiseChanged(true).Succeeded(),
            "Cruise activation did not dispatch the course request");

        Require(commands.calls.size() == 3,
            "hold flow issued the wrong number of commands");
        Require(commands.calls[0].command == RecordedCommand::CloseMap,
            "hold flow did not close the map first");
        Require(commands.calls[1].command == RecordedCommand::PressCruise,
            "hold flow did not press Cruise second");
        Require(commands.calls[2].command == RecordedCommand::RequestCourse,
            "hold flow did not request the course third");
        Require(commands.calls[2].courseId == JemisonId,
            "hold flow requested the wrong course identity");

        const auto locked = controller.OnCourseLockChanged(JemisonId);

        Require(locked.Succeeded(),
            "exact course lock was not handled");
        Require(locked.completedCount == 0,
            "course-lock confirmation emitted an unexpected command");
        Require(controller.CurrentNavigationState().phase ==
                ::NavigationPhase::CourseLocked,
            "full hold flow did not reach CourseLocked");
    }

    void TestDelayedBodyIndexCanBeRefreshed()
    {
        FakeBodyResolutionSource bodySource;
        bodySource.indexReady = false;
        bodySource.indexedBody.reset();
        FakeCruiseCommands commands;
        ::CruiseController controller{ bodySource, commands };
        OpenMap(controller);

        const auto loadingAction = controller.CurrentMapAction(
            ReadyEnvironment());

        Require(!loadingAction.CanHandleInput(),
            "loading body index produced an actionable destination");
        Require(loadingAction.selectionReason ==
                ::SelectionReason::TargetDataLoading,
            "loading body index produced the wrong action reason");
        Require(bodySource.findBodyCalls == 0,
            "loading body index was queried for an entry");

        bodySource.indexReady = true;
        bodySource.indexedBody = ::IndexedBodyObservation{
            .id = JemisonId,
            .systemId = AlphaCentauriId,
        };

        Require(controller.RefreshBodyResolution(
            CurrentIdentity,
            JemisonDossier()),
            "ready body index refresh was rejected");

        const auto readyAction = controller.CurrentMapAction(
            ReadyEnvironment());

        Require(readyAction.CanHandleInput(),
            "refreshed body resolution did not enable the action");
        Require(bodySource.findBodyCalls == 1,
            "ready body index was not queried exactly once");
    }

    void TestStaleEventsDoNotResolveOrDispatch()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseController controller{ bodySource, commands };
        OpenMap(controller);

        const auto liveCallsBefore = bodySource.liveBodyCalls;
        const auto readyCallsBefore = bodySource.indexReadyCalls;
        const auto findCallsBefore = bodySource.findBodyCalls;

        const ::MapSessionIdentity staleIdentity{
            .session = CurrentIdentity.session - 1,
            .generation = CurrentIdentity.generation,
        };

        const bool dossierAccepted = controller.OnDossierChanged(
            staleIdentity,
            {
                .id = MarsId,
                .kind = ::ObservedTargetKind::Planet,
                .displayName = "Mars",
            });

        Require(!dossierAccepted,
            "stale dossier was accepted");
        Require(bodySource.liveBodyCalls == liveCallsBefore &&
                bodySource.indexReadyCalls == readyCallsBefore &&
                bodySource.findBodyCalls == findCallsBefore,
            "stale dossier queried the body source");

        const auto activated = controller.ActivateMapAction(
            staleIdentity,
            ::MapActionGesture::Tap,
            ReadyEnvironment());

        Require(!activated.handled,
            "stale action was reported as handled");
        Require(commands.calls.empty(),
            "stale action dispatched a command");
        Require(controller.CurrentNavigationState().phase ==
                ::NavigationPhase::Idle,
            "stale events changed navigation state");
    }

    void TestFailedCloseMapRecoversAutomatically()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        commands.failOn = RecordedCommand::CloseMap;
        ::CruiseController controller{ bodySource, commands };
        OpenMap(controller);

        const auto activated = controller.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::Tap,
            ReadyEnvironment());

        Require(!activated.Succeeded(),
            "failed CloseMap command was reported as successful");
        Require(activated.failedEffect.has_value() &&
                std::get_if<::CloseMap>(&*activated.failedEffect) != nullptr,
            "failed CloseMap command retained the wrong effect");
        Require(controller.CurrentNavigationState().phase ==
                ::NavigationPhase::Idle,
            "failed CloseMap command left navigation stuck");
        Require(!controller.CurrentNavigationState().destination,
            "failed CloseMap command retained an incomplete destination");
    }

    void TestFailedCruisePressFallsBackToMark()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseController controller{ bodySource, commands };
        OpenMap(controller);

        Require(controller.ActivateMapAction(
            CurrentIdentity,
            ::MapActionGesture::HoldCompleted,
            ReadyEnvironment()).Succeeded(),
            "completed hold did not close the map");

        commands.failOn = RecordedCommand::PressCruise;
        const auto closed = controller.OnMapClosed(CurrentIdentity);

        Require(!closed.Succeeded(),
            "failed PressCruise command was reported as successful");
        Require(closed.failedEffect.has_value() &&
                std::get_if<::PressCruise>(&*closed.failedEffect) != nullptr,
            "failed PressCruise command retained the wrong effect");
        Require(controller.CurrentNavigationState().phase ==
                ::NavigationPhase::Marked,
            "failed PressCruise command did not fall back to a mark");
        Require(controller.CurrentNavigationState().destination.has_value(),
            "failed PressCruise command discarded the destination");
    }

    void RunTests()
    {
        TestFullTapFlow();
        TestFullHoldFlow();
        TestDelayedBodyIndexCanBeRefreshed();
        TestStaleEventsDoNotResolveOrDispatch();
        TestFailedCloseMapRecoversAutomatically();
        TestFailedCruisePressFallsBackToMark();
    }
}

void RunCruiseControllerTests()
{
    RunTests();
}
