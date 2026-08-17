#include "Application/CruiseRuntime.h"
#include "Starfield/StarfieldBodyResolutionSource.h"
#include "TestSuites.h"

#include <concepts>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

static_assert(std::derived_from<::StarfieldBodyResolutionSource, ::BodyResolutionSource>);
static_assert(!std::is_abstract_v<::StarfieldBodyResolutionSource>);

namespace
{
    constexpr ::FormID JemisonId = 0x10;
    constexpr ::FormID MarsId = 0x20;
    constexpr ::FormID AlphaCentauriId = 0x100;

    constexpr ::MapSessionIdentity CurrentIdentity {
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
        ::FormID courseId {0};
    };

    class FakeBodyResolutionSource final : public ::BodyResolutionSource
    {
    public:
        std::optional<::ResolvedBody> ResolveBody(::FormID bodyId) const override
        {
            ++calls;
            lastBodyId = bodyId;
            return result;
        }

        std::optional<::ResolvedBody> result {::ResolvedBody {
            .id = JemisonId,
            .systemId = AlphaCentauriId,
        }};

        mutable std::size_t calls {0};
        mutable ::FormID lastBodyId {0};
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
            throw std::runtime_error {std::string {message}};
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
        ::CruiseRuntime& runtime,
        ::ObservedCruiseState cruiseState = ::ObservedCruiseState::Inactive,
        std::optional<::FormID> currentSystemId = AlphaCentauriId)
    {
        runtime.OnMapMovieCreated(CurrentIdentity.generation);

        Require(
            runtime.OnMapOpened({
                .identity = CurrentIdentity,
                .flying = true,
                .cruiseState = cruiseState,
                .currentSystemId = currentSystemId,
            }),
            "runtime rejected the map session"
        );

        Require(runtime.OnMapViewChanged(CurrentIdentity, ::MapView::System), "runtime rejected system view");

        Require(
            runtime.OnMarkersChanged(
                CurrentIdentity,
                {
                    .highlightedCount = 1,
                    .highlighted =
                        {
                            .id = JemisonId,
                            .kind = ::ObservedTargetKind::Planet,
                            .displayName = "Jemison Marker",
                        },
                }
            ),
            "runtime rejected marker observation"
        );

        Require(runtime.OnDossierChanged(CurrentIdentity, JemisonDossier()), "runtime rejected dossier observation");
    }

    void TestFullTapFlow()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        const auto action = runtime.CurrentMapAction(ReadyEnvironment());

        Require(action.CanHandleInput(), "resolved dossier did not produce an action");
        Require(bodySource.calls == 1 && bodySource.lastBodyId == JemisonId, "dossier did not perform one exact body lookup");

        const auto activated = runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment());

        Require(activated.Succeeded(), "tap command dispatch did not succeed");
        Require(commands.calls.size() == 1 && commands.calls[0].command == RecordedCommand::CloseMap, "tap did not dispatch exactly one CloseMap command");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::ClosingMap, "tap did not wait for map close");

        const auto closed = runtime.OnMapClosed(CurrentIdentity);

        Require(closed.Succeeded(), "tap map-close observation was not handled");
        Require(commands.calls.size() == 1, "plain mark issued an unexpected command after map close");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Marked, "tap did not finish as a retained mark");
    }

    void TestMovieReplacementCancelsOnlyPendingMapSelection()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).Succeeded(), "replacement setup did not dispatch CloseMap");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::ClosingMap, "replacement setup did not wait for map close");

        runtime.OnMapMovieCreated(CurrentIdentity.generation + 1);

        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "movie replacement retained an incomplete selection");
        Require(!runtime.CurrentNavigationState().destination, "movie replacement retained an incomplete destination");
        Require(!runtime.OnMapClosed(CurrentIdentity).handled, "old movie close advanced an invalidated selection");

        FakeCruiseCommands stableCommands;
        ::CruiseRuntime stableRuntime {bodySource, stableCommands};
        OpenMap(stableRuntime);
        Require(stableRuntime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).Succeeded(), "stable-state setup did not dispatch CloseMap");
        Require(stableRuntime.OnMapClosed(CurrentIdentity).Succeeded(), "stable-state setup did not finish the mark");

        stableRuntime.OnMapMovieCreated(CurrentIdentity.generation + 1);

        Require(stableRuntime.CurrentNavigationState().phase == ::NavigationPhase::Marked, "movie replacement discarded a stable mark");
        Require(stableRuntime.CurrentNavigationState().destination.has_value(), "movie replacement lost the marked destination");
    }

    void TestCurrentSelectionReportsAndInvalidatesReadOnlyState()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};

        const auto initial = runtime.CurrentSelection();
        Require(initial.availability == ::SelectionAvailability::Hidden, "unopened runtime exposed a selection");
        Require(initial.reason == ::SelectionReason::InactiveContext, "unopened runtime reported the wrong selection reason");

        runtime.OnMapMovieCreated(CurrentIdentity.generation);
        Require(
            runtime.OnMapOpened({
                .identity = CurrentIdentity,
                .flying = true,
                .cruiseState = ::ObservedCruiseState::Unknown,
                .currentSystemId = AlphaCentauriId,
            }),
            "selection-proof session did not open"
        );
        Require(runtime.OnMapViewChanged(CurrentIdentity, ::MapView::System), "selection-proof system view was rejected");

        const auto empty = runtime.CurrentSelection();
        Require(empty.availability == ::SelectionAvailability::Disabled, "empty system view did not disable selection");
        Require(empty.reason == ::SelectionReason::SelectDestination, "empty system view reported the wrong reason");

        Require(
            runtime.OnMarkersChanged(
                CurrentIdentity,
                {
                    .highlightedCount = 1,
                    .highlighted = {
                        .id = JemisonId,
                        .kind = ::ObservedTargetKind::Planet,
                        .displayName = "Jemison Marker",
                    },
                }
            ),
            "selection-proof marker was rejected"
        );

        const auto markerOnly = runtime.CurrentSelection();
        Require(markerOnly.availability == ::SelectionAvailability::Disabled, "marker-only evidence became actionable");
        Require(markerOnly.reason == ::SelectionReason::TargetDataUpdating, "marker-only evidence reported the wrong reason");

        Require(runtime.OnDossierChanged(CurrentIdentity, JemisonDossier()), "selection-proof dossier was rejected");

        const auto eligible = runtime.CurrentSelection();
        Require(eligible.IsEligible(), "coherent runtime observations did not become eligible");
        Require(eligible.destination->targetId == JemisonId, "selection proof retained the wrong target");
        Require(eligible.destination->systemId == ::FormID {AlphaCentauriId}, "selection proof retained the wrong system");
        Require(eligible.destination->displayName == "Jemison", "selection proof retained the wrong display name");
        Require(commands.calls.empty(), "read-only selection proof dispatched a command");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "read-only selection proof changed navigation state");

        const auto bodyCalls = bodySource.calls;
        Require(runtime.CurrentSelection().IsEligible(), "repeated selection read lost eligibility");
        Require(bodySource.calls == bodyCalls, "reading current selection queried the engine again");

        Require(runtime.OnMapViewChanged(CurrentIdentity, ::MapView::Galaxy), "selection-proof galaxy view was rejected");
        const auto hidden = runtime.CurrentSelection();
        Require(hidden.availability == ::SelectionAvailability::Hidden, "galaxy view retained a visible selection");
        Require(hidden.reason == ::SelectionReason::InactiveContext, "galaxy view reported the wrong selection reason");
        Require(!hidden.destination, "galaxy view retained a destination");

        Require(runtime.OnMapClosed(CurrentIdentity).handled == false, "read-only map close unexpectedly advanced navigation");
        Require(runtime.CurrentSelection().reason == ::SelectionReason::InactiveContext, "closed map retained selection state");

        runtime.OnMapMovieCreated(CurrentIdentity.generation + 1);
        Require(
            !runtime.OnMarkersChanged(
                CurrentIdentity,
                {
                    .highlightedCount = 1,
                    .highlighted = JemisonDossier(),
                }
            ),
            "old movie identity restored stale target evidence"
        );
        Require(runtime.CurrentSelection().reason == ::SelectionReason::InactiveContext, "movie replacement restored stale selection state");
    }

    void TestFullHoldFlow()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::HoldCompleted, ReadyEnvironment()).Succeeded(), "completed hold did not close the map");
        Require(runtime.OnMapClosed(CurrentIdentity).Succeeded(), "map close did not dispatch Cruise activation");
        Require(runtime.OnCruiseChanged(true).Succeeded(), "Cruise activation did not dispatch the course request");

        Require(commands.calls.size() == 3, "hold flow issued the wrong number of commands");
        Require(commands.calls[0].command == RecordedCommand::CloseMap, "hold flow did not close the map first");
        Require(commands.calls[1].command == RecordedCommand::PressCruise, "hold flow did not press Cruise second");
        Require(commands.calls[2].command == RecordedCommand::RequestCourse, "hold flow did not request the course third");
        Require(commands.calls[2].courseId == JemisonId, "hold flow requested the wrong course identity");

        const auto locked = runtime.OnCourseLockChanged(JemisonId);

        Require(locked.Succeeded(), "exact course lock was not handled");
        Require(commands.calls.size() == 3, "course-lock confirmation dispatched an unexpected command");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::CourseLocked, "hold flow did not reach CourseLocked");
    }

    void TestTapOnlyRejectsHold()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        auto environment = ReadyEnvironment();
        environment.cruiseEngageAvailable = false;

        Require(runtime.CurrentMapAction(environment).control == ::ActionControl::TapOnly, "unavailable Cruise did not reduce the action to tap-only");
        Require(!runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::HoldCompleted, environment).handled, "tap-only action accepted a hold");
        Require(commands.calls.empty(), "rejected hold dispatched a command");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "rejected hold changed navigation state");

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, environment).Succeeded(), "tap-only action rejected a tap");
    }

    void TestAlreadyCruisingRequestsCourseAfterClose()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime, ::ObservedCruiseState::Active);

        Require(runtime.CurrentMapAction(ReadyEnvironment()).control == ::ActionControl::TapOnly, "active Cruise did not reduce the action to tap-only");
        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).Succeeded(), "active-Cruise tap did not close the map");
        Require(runtime.OnMapClosed(CurrentIdentity).Succeeded(), "active-Cruise map close did not request the course");

        Require(commands.calls.size() == 2, "active-Cruise flow issued the wrong number of commands");
        Require(commands.calls[0].command == RecordedCommand::CloseMap, "active-Cruise flow did not close the map first");
        Require(commands.calls[1].command == RecordedCommand::RequestCourse, "active-Cruise flow did not request the course second");
        Require(commands.calls[1].courseId == JemisonId, "active-Cruise flow requested the wrong course");
    }

    void TestUnknownCruiseStateCanOnlyMark()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime, ::ObservedCruiseState::Unknown);

        const auto action = runtime.CurrentMapAction(ReadyEnvironment());
        Require(action.control == ::ActionControl::TapOnly, "unknown Cruise state exposed a hold action");
        Require(!runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::HoldCompleted, ReadyEnvironment()).handled, "unknown Cruise state accepted a hold");
        Require(commands.calls.empty(), "rejected unknown-state hold dispatched a command");

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).Succeeded(), "unknown Cruise state rejected safe marking");
        Require(runtime.OnMapClosed(CurrentIdentity).Succeeded(), "unknown Cruise state did not finish marking");
        Require(commands.calls.size() == 1, "unknown Cruise state dispatched a Cruise command");
        Require(commands.calls[0].command == RecordedCommand::CloseMap, "unknown Cruise state dispatched the wrong command");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Marked, "unknown Cruise state did not finish as a mark");
    }

    void TestMissingBodySystemFailsClosed()
    {
        FakeBodyResolutionSource bodySource;
        bodySource.result.reset();
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        const auto action = runtime.CurrentMapAction(ReadyEnvironment());

        Require(!action.CanHandleInput(), "missing body system produced an actionable destination");
        Require(action.selectionReason == ::SelectionReason::TargetSystemUnavailable, "missing body system produced the wrong reason");
        Require(bodySource.calls == 1, "missing body system was not queried exactly once");
    }

    void TestUnsupportedAndStaleDossiersDoNotQueryEngine()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        const auto callsBefore = bodySource.calls;

        Require(
            runtime.OnDossierChanged(
                CurrentIdentity,
                {
                    .id = MarsId,
                    .kind = ::ObservedTargetKind::Unsupported,
                }
            ),
            "unsupported dossier did not update the active session"
        );
        Require(bodySource.calls == callsBefore, "unsupported dossier queried the engine");

        const ::MapSessionIdentity staleIdentity {
            .session = CurrentIdentity.session - 1,
            .generation = CurrentIdentity.generation,
        };

        Require(!runtime.OnDossierChanged(staleIdentity, JemisonDossier()), "stale dossier was accepted");
        Require(bodySource.calls == callsBefore, "stale dossier queried the engine");
        Require(!runtime.ActivateMapAction(staleIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).handled, "stale action was handled");
        Require(commands.calls.empty(), "stale action dispatched a command");
    }

    void TestSolSystemIsEligible()
    {
        FakeBodyResolutionSource bodySource;
        bodySource.result->systemId = 0;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime, ::ObservedCruiseState::Inactive, ::FormID {0});

        Require(runtime.CurrentMapAction(ReadyEnvironment()).CanHandleInput(), "valid Sol system zero was rejected by the runtime");
    }

    void TestFailedCloseMapRecoversAutomatically()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        commands.failOn = RecordedCommand::CloseMap;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        const auto activated = runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment());

        Require(!activated.Succeeded(), "failed CloseMap command was reported as successful");
        Require(activated.failedEffect && std::get_if<::CloseMap>(&*activated.failedEffect), "failed CloseMap retained the wrong effect");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "failed CloseMap left navigation stuck");
        Require(!runtime.CurrentNavigationState().destination, "failed CloseMap retained an incomplete destination");
    }

    void TestFailedCruisePressFallsBackToMark()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::HoldCompleted, ReadyEnvironment()).Succeeded(), "completed hold did not close the map");

        commands.failOn = RecordedCommand::PressCruise;
        const auto closed = runtime.OnMapClosed(CurrentIdentity);

        Require(!closed.Succeeded(), "failed PressCruise command was reported as successful");
        Require(closed.failedEffect && std::get_if<::PressCruise>(&*closed.failedEffect), "failed PressCruise retained the wrong effect");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Marked, "failed PressCruise did not fall back to a mark");
        Require(runtime.CurrentNavigationState().destination.has_value(), "failed PressCruise discarded the destination");
    }

    void TestFailedCourseRequestFallsBackToMark()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime, ::ObservedCruiseState::Active);

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).Succeeded(), "active-Cruise tap did not close the map");

        commands.failOn = RecordedCommand::RequestCourse;
        const auto closed = runtime.OnMapClosed(CurrentIdentity);

        Require(!closed.Succeeded(), "failed RequestCourse command was reported as successful");
        Require(closed.failedEffect && std::get_if<::RequestCourse>(&*closed.failedEffect), "failed RequestCourse retained the wrong effect");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Marked, "failed RequestCourse did not fall back to a mark");
        Require(runtime.CurrentNavigationState().destination.has_value(), "failed RequestCourse discarded the destination");
    }

    void TestMapCloseTimeoutRecoversCurrentSelectionAndRejectsStaleIdentity()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).Succeeded(), "tap did not dispatch CloseMap");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::ClosingMap, "accepted CloseMap did not wait for confirmation");

        const ::MapSessionIdentity staleIdentity {
            .session = CurrentIdentity.session - 1,
            .generation = CurrentIdentity.generation,
        };

        Require(!runtime.OnMapCloseTimedOut(staleIdentity), "stale map-close timeout was accepted");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::ClosingMap, "stale map-close timeout changed the current phase");
        Require(runtime.CurrentNavigationState().destination.has_value(), "stale map-close timeout discarded the current destination");

        Require(runtime.OnMapCloseTimedOut(CurrentIdentity), "current map-close timeout was not recovered");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "map-close timeout left navigation stuck");
        Require(!runtime.CurrentNavigationState().destination, "map-close timeout retained an incomplete destination");
        Require(commands.calls.size() == 1, "map-close timeout dispatched an unexpected command");
        Require(!runtime.OnMapCloseTimedOut(CurrentIdentity), "repeated map-close timeout was accepted");
    }

    void TestCruiseActivationTimeoutFallsBackAndRejectsLateObservation()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::HoldCompleted, ReadyEnvironment()).Succeeded(), "completed hold did not dispatch CloseMap");
        Require(runtime.OnMapClosed(CurrentIdentity).Succeeded(), "map close did not dispatch PressCruise");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::CruiseRequested, "accepted PressCruise did not wait for activation");

        Require(runtime.OnCruiseActivationTimedOut(), "Cruise activation timeout was not recovered");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Marked, "Cruise activation timeout did not fall back to a mark");
        Require(runtime.CurrentNavigationState().destination.has_value(), "Cruise activation timeout discarded the destination");
        Require(commands.calls.size() == 2, "Cruise activation timeout dispatched an unexpected command");
        Require(!runtime.OnCruiseActivationTimedOut(), "repeated Cruise activation timeout was accepted");

        Require(runtime.OnCruiseChanged(true).Succeeded(), "late Cruise activation did not request the course from the retained mark");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::AwaitingCourseLock, "late Cruise activation did not advance to AwaitingCourseLock");
        Require(!runtime.OnCruiseActivationTimedOut(), "stale Cruise activation timeout changed a newer phase");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::AwaitingCourseLock, "stale Cruise activation timeout changed navigation state");
    }

    void TestCourseLockTimeoutRequiresExactCourseAndFallsBack()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime, ::ObservedCruiseState::Active);

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).Succeeded(), "active-Cruise tap did not dispatch CloseMap");
        Require(runtime.OnMapClosed(CurrentIdentity).Succeeded(), "map close did not dispatch RequestCourse");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::AwaitingCourseLock, "accepted RequestCourse did not wait for the exact lock");

        Require(!runtime.OnCourseLockTimedOut(MarsId), "wrong-course timeout was accepted");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::AwaitingCourseLock, "wrong-course timeout changed the current phase");
        Require(runtime.CurrentNavigationState().destination && runtime.CurrentNavigationState().destination->courseId == JemisonId, "wrong-course timeout changed the destination");

        Require(runtime.OnCourseLockTimedOut(JemisonId), "exact course-lock timeout was not recovered");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Marked, "course-lock timeout did not fall back to a mark");
        Require(runtime.CurrentNavigationState().destination && runtime.CurrentNavigationState().destination->courseId == JemisonId, "course-lock timeout discarded the destination");
        Require(commands.calls.size() == 2, "course-lock timeout dispatched an unexpected command");
        Require(!runtime.OnCourseLockTimedOut(JemisonId), "repeated course-lock timeout was accepted");

        Require(runtime.OnCruiseChanged(true).Succeeded(), "Cruise reactivation did not request the retained course");
        Require(runtime.OnCourseLockChanged(JemisonId).Succeeded(), "exact course lock was not confirmed");
        Require(!runtime.OnCourseLockTimedOut(JemisonId), "late course-lock timeout was accepted after confirmation");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::CourseLocked, "late course-lock timeout changed the confirmed lock");
    }

    void RunTests()
    {
        TestFullTapFlow();
        TestMovieReplacementCancelsOnlyPendingMapSelection();
        TestCurrentSelectionReportsAndInvalidatesReadOnlyState();
        TestFullHoldFlow();
        TestTapOnlyRejectsHold();
        TestAlreadyCruisingRequestsCourseAfterClose();
        TestUnknownCruiseStateCanOnlyMark();
        TestMissingBodySystemFailsClosed();
        TestUnsupportedAndStaleDossiersDoNotQueryEngine();
        TestSolSystemIsEligible();
        TestFailedCloseMapRecoversAutomatically();
        TestFailedCruisePressFallsBackToMark();
        TestFailedCourseRequestFallsBackToMark();
        TestMapCloseTimeoutRecoversCurrentSelectionAndRejectsStaleIdentity();
        TestCruiseActivationTimeoutFallsBackAndRejectsLateObservation();
        TestCourseLockTimeoutRequiresExactCourseAndFallsBack();
    }
}

void RunCruiseRuntimeTests()
{
    RunTests();
}
