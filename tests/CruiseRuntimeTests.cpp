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
    constexpr ::FormID AlphaCentauriFormId = 0x5E60A;
    constexpr ::FormID ChawlaId = 0x5E315;
    constexpr ::FormID ChawlaParentId = 0x5E313;
    constexpr ::FormID CheyenneId = 0x11AF0;
    constexpr ::FormID CheyenneFormId = 0x5E607;
    constexpr ::FormID TheEyeMapId = 0x1285A;
    constexpr ::FormID TheEyeTargetId = 0x12894;
    constexpr ::FormID TheEyeCourseId = 0x12895;

    constexpr ::MapSessionIdentity CurrentIdentity {
        .session = 7,
        .generation = 3,
    };

    enum class RecordedCommand
    {
        CloseMap,
        BeginRemoteRoute,
        AssignStationTarget,
        PressCruise,
        RequestCourse,
    };

    struct RecordedCall
    {
        RecordedCommand command;
        ::FormID courseId {0};
        ::OperationId operationId {0};
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
            .system = {.starFormId = AlphaCentauriFormId, .numericId = AlphaCentauriId},
            .remotePlan = ::RemoteTargetPlan {},
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

        bool BeginRemoteRoute(const ::BeginRemoteRoute& effect) override
        {
            return Record(RecordedCommand::BeginRemoteRoute, effect.destination.courseId, effect.operationId);
        }

        bool PressCruise(::OperationId operationId) override
        {
            return Record(RecordedCommand::PressCruise, 0, operationId);
        }

        bool AssignStationTarget(::FormID targetId) override
        {
            return Record(RecordedCommand::AssignStationTarget, targetId);
        }

        bool RequestCourse(::FormID courseId, ::OperationId operationId) override
        {
            return Record(RecordedCommand::RequestCourse, courseId, operationId);
        }

        std::optional<RecordedCommand> failOn;
        std::vector<RecordedCall> calls;

    private:
        bool Record(RecordedCommand command, ::FormID courseId = 0, ::OperationId operationId = 0)
        {
            calls.push_back({
                .command = command,
                .courseId = courseId,
                .operationId = operationId,
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
            .remoteRoutingAvailable = true,
        };
    }

    void OpenMap(::CruiseRuntime& runtime, ::ObservedCruiseState cruiseState = ::ObservedCruiseState::Inactive, std::optional<::FormID> currentSystemId = AlphaCentauriId)
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
        const auto starFormId = currentSystemId == ::FormID {0} ? ::FormID {0x5E5CB} : AlphaCentauriFormId;
        Require(runtime.OnCurrentSystemFormObserved(CurrentIdentity, starFormId), "runtime rejected current-system STDT");

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

    void OpenStationMap(::CruiseRuntime& runtime, ::ObservedCruiseState cruiseState = ::ObservedCruiseState::Inactive)
    {
        runtime.OnMapMovieCreated(CurrentIdentity.generation);
        Require(
            runtime.OnMapOpened({
                .identity = CurrentIdentity,
                .flying = true,
                .cruiseState = cruiseState,
                .currentSystemId = AlphaCentauriId,
            }),
            "runtime rejected the station map session"
        );
        Require(runtime.OnCurrentSystemFormObserved(CurrentIdentity, AlphaCentauriFormId), "runtime rejected the current-system form");
        Require(runtime.OnMapViewChanged(CurrentIdentity, ::MapView::System), "runtime rejected station system view");
        Require(
            runtime.OnMarkersChanged(
                CurrentIdentity,
                {
                    .highlightedCount = 1,
                    .highlighted =
                        {
                            .id = TheEyeMapId,
                            .kind = ::ObservedTargetKind::Station,
                            .displayName = "The Eye",
                            .resolvedTargetId = TheEyeTargetId,
                            .resolvedCourseId = TheEyeCourseId,
                            .displayedSystemFormId = AlphaCentauriFormId,
                        },
                }
            ),
            "runtime rejected station marker observation"
        );
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

    void TestStationAssignmentPrecedesCourseRequest()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenStationMap(runtime, ::ObservedCruiseState::Active);

        Require(runtime.CurrentMapAction(ReadyEnvironment()).CanHandleInput(), "resolved station did not produce an action");
        Require(bodySource.calls == 0, "station selection queried the planetary body resolver");
        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment()).Succeeded(), "station tap did not close the map");

        const auto closed = runtime.OnMapClosed(CurrentIdentity);

        Require(closed.Succeeded(), "station map close did not complete");
        Require(commands.calls.size() == 3, "station map close issued the wrong command count");
        Require(commands.calls[0].command == RecordedCommand::CloseMap, "station selection did not close the map first");
        Require(commands.calls[1].command == RecordedCommand::AssignStationTarget && commands.calls[1].courseId == TheEyeTargetId, "station target was not assigned before course dispatch");
        Require(commands.calls[2].command == RecordedCommand::RequestCourse && commands.calls[2].courseId == TheEyeCourseId, "station course request did not use the distinct course marker");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::AwaitingCourseLock, "station course request entered the wrong phase");
    }

    void TestFailedStationAssignmentDiscardsSelection()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        commands.failOn = RecordedCommand::AssignStationTarget;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenStationMap(runtime);

        Require(runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::HoldCompleted, ReadyEnvironment()).Succeeded(), "station hold did not close the map");

        const auto closed = runtime.OnMapClosed(CurrentIdentity);

        Require(!closed.Succeeded() && closed.targetAssignmentFailed, "failed station assignment was reported as successful");
        Require(commands.calls.size() == 2, "failed station assignment dispatched a later command");
        Require(commands.calls[1].command == RecordedCommand::AssignStationTarget, "failed station assignment recorded the wrong command");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "failed station assignment left navigation active");
        Require(!runtime.CurrentNavigationState().destination, "failed station assignment retained the destination");
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
        Require(runtime.OnCurrentSystemFormObserved(CurrentIdentity, AlphaCentauriFormId), "selection-proof STDT was rejected");

        const auto empty = runtime.CurrentSelection();
        Require(empty.availability == ::SelectionAvailability::Disabled, "empty system view did not disable selection");
        Require(empty.reason == ::SelectionReason::SelectDestination, "empty system view reported the wrong reason");

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
            "selection-proof marker was rejected"
        );

        const auto markerOnly = runtime.CurrentSelection();
        Require(markerOnly.availability == ::SelectionAvailability::Disabled, "marker-only evidence became actionable");
        Require(markerOnly.reason == ::SelectionReason::TargetDataUpdating, "marker-only evidence reported the wrong reason");

        Require(runtime.OnDossierChanged(CurrentIdentity, JemisonDossier()), "selection-proof dossier was rejected");

        const auto eligible = runtime.CurrentSelection();
        Require(eligible.IsEligible(), "coherent runtime observations did not become eligible");
        Require(eligible.destination->targetId == JemisonId, "selection proof retained the wrong target");
        Require(eligible.destination->system.numericId == ::FormID {AlphaCentauriId}, "selection proof retained the wrong system");
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

        Require(runtime.OnDossierChanged(CurrentIdentity, {}), "zero dossier did not update the active session");
        Require(bodySource.calls == callsBefore, "zero dossier queried the engine");

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
        bodySource.result->system = {.starFormId = 0x5E5CB, .numericId = 0};
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

        Require(!runtime.OnMapCloseTimedOut(CurrentIdentity), "active map without a pending close accepted a timeout");
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

    void TestRemoteTapRunsOneCorrelatedLifecycle()
    {
        FakeBodyResolutionSource bodySource;
        bodySource.result = ::ResolvedBody {
            .id = JemisonId,
            .system = {.starFormId = CheyenneFormId, .numericId = CheyenneId},
            .remotePlan = ::RemoteTargetPlan {
                .allowedWaypointIds = {ChawlaParentId},
            },
        };
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        const auto action = runtime.CurrentMapAction(ReadyEnvironment());
        Require(action.CanHandleInput() && action.requiresTravel, "remote body did not produce a travel action");
        Require(action.control == ::ActionControl::TapOnly, "remote action exposed a hold gesture");

        const auto activated = runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment());
        Require(activated.Succeeded(), "remote tap did not dispatch its route effect");
        Require(commands.calls.size() == 1 && commands.calls[0].command == RecordedCommand::BeginRemoteRoute, "remote tap did not dispatch exactly one BeginRemoteRoute command");
        const auto operationId = commands.calls[0].operationId;
        Require(operationId != 0, "remote command lost its operation ID");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::RoutingRemote, "remote tap did not enter RoutingRemote");

        Require(!runtime.OnRemoteRouteCommitted(operationId + 1, CurrentIdentity).handled, "stale route commitment was accepted");
        Require(runtime.OnRemoteRouteCommitted(operationId, CurrentIdentity).Succeeded(), "exact route commitment was rejected");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::PendingRemoteArrival, "route commitment did not enter PendingRemoteArrival");

        ::RemoteArrivalObservation intermediate {
            .operationId = operationId,
            .currentSystem = {.starFormId = AlphaCentauriFormId, .numericId = AlphaCentauriId},
            .mapClosed = true,
            .loadingMenuClosed = true,
            .completedPlayerJump = true,
            .settledFlight = true,
            .flying = true,
            .freshHudPublication = true,
            .courseRows = {JemisonId},
        };
        Require(!runtime.OnRemoteArrival(std::move(intermediate)).handled, "an intermediate system was accepted as final arrival");

        ::RemoteArrivalObservation finalArrival {
            .operationId = operationId,
            .currentSystem = {.starFormId = CheyenneFormId, .numericId = CheyenneId},
            .mapClosed = true,
            .loadingMenuClosed = true,
            .completedPlayerJump = true,
            .settledFlight = true,
            .flying = true,
            .freshHudPublication = true,
            .courseRows = {JemisonId},
        };
        Require(runtime.OnRemoteArrival(std::move(finalArrival)).Succeeded(), "exact final arrival did not dispatch Cruise activation");
        Require(commands.calls.size() == 2 && commands.calls[1].command == RecordedCommand::PressCruise && commands.calls[1].operationId == operationId, "final arrival did not issue one correlated Cruise press");

        Require(runtime.OnCruiseChanged(true).Succeeded(), "remote Cruise activation did not dispatch the final course request");
        Require(
            commands.calls.size() == 3 && commands.calls[2].command == RecordedCommand::RequestCourse && commands.calls[2].courseId == JemisonId && commands.calls[2].operationId == operationId,
            "remote lifecycle requested a waypoint or lost correlation"
        );
        Require(runtime.OnCourseLockChanged(JemisonId).Succeeded(), "exact remote final lock was rejected");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::CourseLocked && !runtime.CurrentNavigationState().remoteOperation, "completed remote lifecycle retained asynchronous ownership");
    }

    void ConfigureRemoteBody(FakeBodyResolutionSource& bodySource)
    {
        bodySource.result = ::ResolvedBody {
            .id = JemisonId,
            .system = {.starFormId = CheyenneFormId, .numericId = CheyenneId},
            .remotePlan = ::RemoteTargetPlan {
                .allowedWaypointIds = {ChawlaParentId},
            },
        };
    }

    ::OperationId ActivateRemote(::CruiseRuntime& runtime, FakeCruiseCommands& commands)
    {
        const auto result = runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment());
        Require(result.Succeeded(), "remote setup did not dispatch BeginRemoteRoute");
        Require(!commands.calls.empty() && commands.calls.back().command == RecordedCommand::BeginRemoteRoute, "remote setup dispatched the wrong command");
        return commands.calls.back().operationId;
    }

    ::RemoteArrivalObservation ReadyRemoteArrival(::OperationId operationId)
    {
        return {
            .operationId = operationId,
            .currentSystem = {.starFormId = CheyenneFormId, .numericId = CheyenneId},
            .mapClosed = true,
            .loadingMenuClosed = true,
            .completedPlayerJump = true,
            .settledFlight = true,
            .flying = true,
            .freshHudPublication = true,
            .courseRows = {JemisonId},
        };
    }

    void TestDisabledActiveActionDoesNotDispatch()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        auto environment = ReadyEnvironment();
        environment.cruiseControlBound = false;
        Require(!runtime.CurrentMapAction(environment).CanHandleInput(), "unbound active action remained actionable");
        Require(!runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, environment).handled, "disabled active action was handled");
        Require(commands.calls.empty(), "disabled active action dispatched a command");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "disabled active action changed navigation state");
    }

    void TestFailedRemoteRouteDispatchRecoversAutomatically()
    {
        FakeBodyResolutionSource bodySource;
        ConfigureRemoteBody(bodySource);
        FakeCruiseCommands commands;
        commands.failOn = RecordedCommand::BeginRemoteRoute;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);

        const auto activated = runtime.ActivateMapAction(CurrentIdentity, ::MapActionGesture::Tap, ReadyEnvironment());
        Require(!activated.Succeeded(), "failed remote-route dispatch reported success");
        Require(activated.failedEffect && std::get_if<::BeginRemoteRoute>(&*activated.failedEffect), "failed remote-route dispatch retained the wrong effect");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle && !runtime.CurrentNavigationState().remoteOperation, "failed remote-route dispatch retained ownership");
    }

    void TestRemoteFailureAndCancellationCallbacksAreCorrelated()
    {
        {
            FakeBodyResolutionSource bodySource;
            ConfigureRemoteBody(bodySource);
            FakeCruiseCommands commands;
            ::CruiseRuntime runtime {bodySource, commands};
            OpenMap(runtime);
            const auto operationId = ActivateRemote(runtime, commands);

            const ::MapSessionIdentity wrongIdentity {
                .session = CurrentIdentity.session + 1,
                .generation = CurrentIdentity.generation,
            };
            Require(!runtime.OnRemoteRouteFailed(operationId, wrongIdentity).handled, "wrong-session route failure was handled");
            Require(!runtime.OnRemoteRouteFailed(operationId + 1, CurrentIdentity).handled, "wrong-operation route failure was handled");
            Require(runtime.OnRemoteRouteFailed(operationId, CurrentIdentity).Succeeded(), "exact route failure was rejected");
            Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "exact route failure retained navigation state");
        }
        {
            FakeBodyResolutionSource bodySource;
            ConfigureRemoteBody(bodySource);
            FakeCruiseCommands commands;
            ::CruiseRuntime runtime {bodySource, commands};
            OpenMap(runtime);
            const auto operationId = ActivateRemote(runtime, commands);

            Require(!runtime.OnRemoteOperationCancelled(operationId + 1), "wrong-operation cancellation was accepted");
            Require(runtime.OnRemoteOperationCancelled(operationId), "exact cancellation was rejected");
            Require(!runtime.OnRemoteOperationCancelled(operationId), "repeated cancellation was accepted");
        }
        {
            FakeBodyResolutionSource bodySource;
            ConfigureRemoteBody(bodySource);
            FakeCruiseCommands commands;
            ::CruiseRuntime runtime {bodySource, commands};
            OpenMap(runtime);
            ActivateRemote(runtime, commands);

            Require(runtime.OnRemoteFlightInvalidated(), "remote-flight invalidation was rejected");
            Require(!runtime.OnRemoteFlightInvalidated(), "repeated remote-flight invalidation was accepted");
        }
        {
            FakeBodyResolutionSource bodySource;
            ConfigureRemoteBody(bodySource);
            FakeCruiseCommands commands;
            ::CruiseRuntime runtime {bodySource, commands};
            Require(!runtime.OnLoadGame(), "idle load reset reported a change");
            OpenMap(runtime);
            ActivateRemote(runtime, commands);

            Require(runtime.OnLoadGame(), "load did not clear remote ownership");
            Require(!runtime.OnLoadGame(), "repeated load reset reported a change");
            Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "load retained remote navigation state");
        }
    }

    void TestRemoteCruiseAndCourseDispatchFailuresClearOwnership()
    {
        {
            FakeBodyResolutionSource bodySource;
            ConfigureRemoteBody(bodySource);
            FakeCruiseCommands commands;
            ::CruiseRuntime runtime {bodySource, commands};
            OpenMap(runtime);
            const auto operationId = ActivateRemote(runtime, commands);
            runtime.OnRemoteRouteCommitted(operationId, CurrentIdentity);

            commands.failOn = RecordedCommand::PressCruise;
            const auto arrival = runtime.OnRemoteArrival(ReadyRemoteArrival(operationId));
            Require(!arrival.Succeeded() && arrival.failedEffect && std::get_if<::PressCruise>(&*arrival.failedEffect), "failed remote Cruise press produced the wrong result");
            Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "failed remote Cruise press retained ownership");
        }
        {
            FakeBodyResolutionSource bodySource;
            ConfigureRemoteBody(bodySource);
            FakeCruiseCommands commands;
            ::CruiseRuntime runtime {bodySource, commands};
            OpenMap(runtime);
            const auto operationId = ActivateRemote(runtime, commands);
            runtime.OnRemoteRouteCommitted(operationId, CurrentIdentity);
            Require(runtime.OnRemoteArrival(ReadyRemoteArrival(operationId)).Succeeded(), "remote course-failure setup did not reach Cruise press");

            commands.failOn = RecordedCommand::RequestCourse;
            const auto activated = runtime.OnCruiseChanged(true);
            Require(!activated.Succeeded() && activated.failedEffect && std::get_if<::RequestCourse>(&*activated.failedEffect), "failed remote course request produced the wrong result");
            Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "failed remote course request retained ownership");
        }
    }

    void TestRemoteCruiseExitTimeoutWrapperIsCorrelated()
    {
        FakeBodyResolutionSource bodySource;
        ConfigureRemoteBody(bodySource);
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);
        const auto operationId = ActivateRemote(runtime, commands);
        runtime.OnRemoteRouteCommitted(operationId, CurrentIdentity);
        Require(runtime.OnRemoteArrival(ReadyRemoteArrival(operationId)).Succeeded(), "remote timeout setup did not reach Cruise preparation");

        Require(!runtime.OnRemoteCruiseExitTimedOut(operationId + 1), "wrong-operation remote Cruise timeout was accepted");
        Require(runtime.OnRemoteCruiseExitTimedOut(operationId), "exact remote Cruise timeout was rejected");
        Require(!runtime.OnRemoteCruiseExitTimedOut(operationId), "repeated remote Cruise timeout was accepted");
        Require(runtime.CurrentNavigationState().phase == ::NavigationPhase::Idle, "remote Cruise timeout retained ownership");
    }

    void TestRuntimeUpdateWrappersRejectStaleIdentity()
    {
        FakeBodyResolutionSource bodySource;
        FakeCruiseCommands commands;
        ::CruiseRuntime runtime {bodySource, commands};
        OpenMap(runtime);
        const ::MapSessionIdentity stale {
            .session = CurrentIdentity.session + 1,
            .generation = CurrentIdentity.generation,
        };

        Require(!runtime.OnMapViewChanged(stale, ::MapView::Galaxy), "stale view update was accepted");
        Require(!runtime.OnMarkersChanged(stale, {}), "stale marker update was accepted");
        Require(!runtime.OnCurrentSystemResolved(stale, MarsId), "stale numeric-system update was accepted");
        Require(!runtime.OnCurrentSystemFormObserved(stale, CheyenneFormId), "stale system-form update was accepted");
        Require(runtime.CurrentSelection().IsEligible(), "stale wrapper calls changed the current selection");
    }

    void RunTests()
    {
        TestFullTapFlow();
        TestStationAssignmentPrecedesCourseRequest();
        TestFailedStationAssignmentDiscardsSelection();
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
        TestRemoteTapRunsOneCorrelatedLifecycle();
        TestDisabledActiveActionDoesNotDispatch();
        TestFailedRemoteRouteDispatchRecoversAutomatically();
        TestRemoteFailureAndCancellationCallbacksAreCorrelated();
        TestRemoteCruiseAndCourseDispatchFailuresClearOwnership();
        TestRemoteCruiseExitTimeoutWrapperIsCorrelated();
        TestRuntimeUpdateWrappersRejectStaleIdentity();
    }
}

void RunCruiseRuntimeTests()
{
    RunTests();
}
