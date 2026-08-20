#include "Navigation/NavigationRuntime.h"
#include "TestSuites.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    template <class T> const T* FindEffect(const ::TransitionResult& result)
    {
        return result.effect ? std::get_if<T>(&*result.effect) : nullptr;
    }

    ::Destination Jemison()
    {
        return ::Destination {
            .kind = ::DestinationKind::Planet,
            .targetId = 0x00000010,
            .courseId = 0x00000010,
            .system = {.starFormId = 0x00001000, .numericId = 0x00000100},
            .displayName = "Jemison",
        };
    }

    ::Destination Mars()
    {
        return ::Destination {
            .kind = ::DestinationKind::Planet,
            .targetId = 0x00000020,
            .courseId = 0x00000020,
            .system = {.starFormId = 0x00001000, .numericId = 0x00000100},
            .displayName = "Mars",
        };
    }

    ::Destination Chawla()
    {
        return ::Destination {
            .kind = ::DestinationKind::Moon,
            .targetId = 0x0005E315,
            .courseId = 0x0005E315,
            .system = {.starFormId = 0x0005E60A, .numericId = 0x00011720},
            .remotePlan = {.allowedWaypointIds = {0x0005E313}},
            .displayName = "Chawla",
        };
    }

    constexpr ::RemoteOperationSource RemoteSource {
        .session = 7,
        .movieGeneration = 3,
    };

    ::OperationId StartRemote(::NavigationRuntime& runtime)
    {
        const auto selected = runtime.SelectDestination(Chawla(), ::SelectionIntent::StartRemoteCruise, false, {.source = RemoteSource, .inputDevice = ::NavigationInputDevice::Gamepad});
        const auto* begin = FindEffect<::BeginRemoteRoute>(selected);
        Require(begin != nullptr, "remote selection did not emit BeginRemoteRoute");
        Require(begin->operationId != 0, "remote operation did not receive a monotonic ID");
        return begin->operationId;
    }

    ::RemoteArrivalObservation ReadyArrival(::OperationId operationId, std::vector<::FormID> rows)
    {
        return {
            .operationId = operationId,
            .currentBodyId = 0x0005E313,
            .currentSystem = {.starFormId = 0x0005E60A, .numericId = 0x00011720},
            .mapClosed = true,
            .loadingMenuClosed = true,
            .completedPlayerJump = true,
            .settledFlight = true,
            .flying = true,
            .freshHudPublication = true,
            .courseRowsComplete = true,
            .courseRows = std::move(rows),
        };
    }

    void TestTapMarksDestination()
    {
        ::NavigationRuntime runtime;

        const auto selected = runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);

        Require(selected.handled, "valid destination was not accepted");
        Require(runtime.CurrentState().phase == ::NavigationPhase::ClosingMap, "selection did not wait for map close");
        Require(FindEffect<::CloseMap>(selected) != nullptr, "selection did not request map close");

        const auto closed = runtime.MapClosed();

        Require(closed.handled, "expected map close was not handled");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "tap did not leave the destination marked");
        Require(runtime.CurrentState().destination.has_value(), "tap lost the selected destination");
        Require(runtime.CurrentState().destination->targetId == Jemison().targetId, "tap retained the wrong destination");
        Require(!closed.effect, "plain tap emitted an unexpected engine effect");
    }

    void TestSameDestinationTogglesOff()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);
        runtime.MapClosed();

        const auto toggled = runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);

        Require(toggled.handled, "same-destination toggle was not handled");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "same-destination toggle did not return to Idle");
        Require(!runtime.CurrentState().destination, "same-destination toggle retained the destination");
        Require(FindEffect<::CloseMap>(toggled) != nullptr, "same-destination toggle did not request map close");
    }

    void TestDestinationReplacement()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);
        runtime.MapClosed();

        runtime.SelectDestination(Mars(), ::SelectionIntent::Mark, false);

        Require(runtime.CurrentState().phase == ::NavigationPhase::ClosingMap, "replacement did not enter ClosingMap");
        Require(runtime.CurrentState().destination.has_value(), "replacement lost the destination");
        Require(runtime.CurrentState().destination->targetId == Mars().targetId, "replacement retained the old destination");

        runtime.MapClosed();

        Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "replacement was not marked after map close");
    }

    void TestMapSelectionInvalidationCancelsIncompleteClose()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);

        Require(runtime.InvalidateMapSelection(), "pending map selection was not invalidated");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "map invalidation left navigation waiting for close");
        Require(!runtime.CurrentState().destination, "map invalidation retained an incomplete destination");
        Require(!runtime.MapClosed().handled, "stale map close advanced an invalidated selection");
        Require(!runtime.InvalidateMapSelection(), "repeated map invalidation changed stable state");
    }

    void TestMapSelectionInvalidationPreservesPostMapWork()
    {
        {
            ::NavigationRuntime runtime;
            runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);
            runtime.MapClosed();

            Require(!runtime.InvalidateMapSelection(), "map invalidation claimed a stable mark");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "map invalidation discarded a stable mark");
            Require(runtime.CurrentState().destination.has_value(), "map invalidation lost a marked destination");
        }

        {
            ::NavigationRuntime runtime;
            runtime.SelectDestination(Jemison(), ::SelectionIntent::StartCruise, false);
            runtime.MapClosed();

            Require(!runtime.InvalidateMapSelection(), "map invalidation claimed an issued Cruise request");
            Require(runtime.CurrentState().phase == ::NavigationPhase::CruiseRequested, "map invalidation discarded an issued Cruise request");
            Require(runtime.CurrentState().destination.has_value(), "map invalidation lost the Cruise destination");
        }

        {
            ::NavigationRuntime runtime;
            runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, true);
            runtime.MapClosed();

            Require(!runtime.InvalidateMapSelection(), "map invalidation claimed an issued course request");
            Require(runtime.CurrentState().phase == ::NavigationPhase::AwaitingCourseLock, "map invalidation discarded an issued course request");
            Require(runtime.CurrentState().destination.has_value(), "map invalidation lost the pending course destination");

            runtime.CourseLockChanged(Jemison().courseId);

            Require(!runtime.InvalidateMapSelection(), "map invalidation claimed a confirmed course lock");
            Require(runtime.CurrentState().phase == ::NavigationPhase::CourseLocked, "map invalidation discarded a confirmed course lock");
            Require(runtime.CurrentState().destination.has_value(), "map invalidation lost the locked destination");
        }
    }

    void TestCompletedHoldUsesExactCourseLock()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Jemison(), ::SelectionIntent::StartCruise, false);

        const auto closed = runtime.MapClosed();

        Require(runtime.CurrentState().phase == ::NavigationPhase::CruiseRequested, "completed hold did not request Cruise");
        Require(FindEffect<::PressCruise>(closed) != nullptr, "completed hold did not emit PressCruise");

        const auto activated = runtime.CruiseChanged(true);
        const auto* course = FindEffect<::RequestCourse>(activated);

        Require(runtime.CurrentState().phase == ::NavigationPhase::AwaitingCourseLock, "Cruise activation did not await course confirmation");
        Require(course != nullptr, "Cruise activation did not request the course");
        Require(course->courseId == Jemison().courseId, "Cruise activation requested the wrong course");

        const auto unrelated = runtime.CourseLockChanged(0xDEADBEEF);

        Require(!unrelated.handled, "unrelated course lock was accepted");
        Require(runtime.CurrentState().phase == ::NavigationPhase::AwaitingCourseLock, "unrelated course lock changed navigation phase");

        const auto exact = runtime.CourseLockChanged(Jemison().courseId);

        Require(exact.handled, "exact course lock was ignored");
        Require(runtime.CurrentState().phase == ::NavigationPhase::CourseLocked, "exact course lock did not establish success");

        const auto lost = runtime.CourseLockChanged(0);

        Require(lost.handled, "exact lock loss was ignored");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "lock loss did not return to Marked");
        Require(runtime.CurrentState().destination.has_value(), "lock loss discarded the destination");
    }

    void TestAlreadyCruisingSkipsCruisePress()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Mars(), ::SelectionIntent::Mark, true);

        const auto closed = runtime.MapClosed();
        const auto* course = FindEffect<::RequestCourse>(closed);

        Require(runtime.CurrentState().phase == ::NavigationPhase::AwaitingCourseLock, "active-Cruise selection did not await course lock");
        Require(FindEffect<::PressCruise>(closed) == nullptr, "active-Cruise selection emitted another Cruise press");
        Require(course != nullptr, "active-Cruise selection did not request a course");
        Require(course->courseId == Mars().courseId, "active-Cruise selection requested the wrong course");
    }

    void TestInvalidDestinationFailsClosed()
    {
        ::NavigationRuntime runtime;

        const auto result = runtime.SelectDestination(::Destination {}, ::SelectionIntent::Mark, false);

        Require(!result.handled, "invalid destination was accepted");
        Require(!result.effect, "invalid destination emitted an effect");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "invalid destination changed navigation phase");
        Require(!runtime.CurrentState().destination, "invalid destination was retained");
    }

    void TestSolDestinationIsValid()
    {
        ::NavigationRuntime runtime;
        auto destination = Jemison();
        destination.system = {.starFormId = 0x0005E5CB, .numericId = 0};

        const auto result = runtime.SelectDestination(destination, ::SelectionIntent::Mark, false);

        Require(result.handled, "valid Sol destination was rejected");
        Require(runtime.CurrentState().destination.has_value(), "valid Sol destination was not retained");
        Require(runtime.CurrentState().destination->system == ::SystemIdentity {.starFormId = 0x0005E5CB, .numericId = 0}, "Sol destination changed system identity");
    }

    void TestCloseMapFailureAbandonsIncompleteSelection()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);

        const bool recovered = runtime.RecoverFromEffectFailure(::CloseMap {});

        Require(recovered, "CloseMap failure was not recovered");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "CloseMap failure left navigation waiting for map close");
        Require(!runtime.CurrentState().destination, "CloseMap failure retained an incomplete selection");
        Require(!runtime.MapClosed().handled, "CloseMap failure retained pending map-close state");
    }

    void TestCruisePressFailureFallsBackToMark()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Jemison(), ::SelectionIntent::StartCruise, false);
        runtime.MapClosed();

        const bool recovered = runtime.RecoverFromEffectFailure(::PressCruise {});

        Require(recovered, "PressCruise failure was not recovered");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "PressCruise failure did not fall back to a mark");
        Require(runtime.CurrentState().destination.has_value(), "PressCruise failure discarded the destination");
        Require(runtime.CurrentState().destination->targetId == Jemison().targetId, "PressCruise failure retained the wrong destination");
    }

    void TestOnlyExactCourseRequestFailureRecovers()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);
        runtime.MapClosed();
        runtime.CruiseChanged(true);

        const bool staleRecovered = runtime.RecoverFromEffectFailure(::RequestCourse {Mars().courseId});

        Require(!staleRecovered, "unrelated RequestCourse failure changed navigation");
        Require(runtime.CurrentState().phase == ::NavigationPhase::AwaitingCourseLock, "unrelated RequestCourse failure left the expected phase");

        const bool exactRecovered = runtime.RecoverFromEffectFailure(::RequestCourse {Jemison().courseId});

        Require(exactRecovered, "exact RequestCourse failure was not recovered");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "RequestCourse failure did not fall back to a mark");
        Require(runtime.CurrentState().destination.has_value(), "RequestCourse failure discarded the destination");
    }

    void TestFailureOutsideOwningPhaseIsIgnored()
    {
        ::NavigationRuntime runtime;

        runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);
        runtime.MapClosed();

        const bool recovered = runtime.RecoverFromEffectFailure(::PressCruise {});

        Require(!recovered, "stale PressCruise failure was accepted");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "stale failure changed navigation phase");
        Require(runtime.CurrentState().destination.has_value(), "stale failure discarded the destination");
    }

    void TestRemoteOperationCorrelationAndIntermediatePreservation()
    {
        ::NavigationRuntime runtime;
        const auto operationId = StartRemote(runtime);

        Require(runtime.CurrentState().phase == ::NavigationPhase::RoutingRemote, "remote selection did not enter RoutingRemote");
        Require(runtime.CurrentState().remoteOperation && runtime.CurrentState().remoteOperation->inputDevice == ::NavigationInputDevice::Gamepad, "remote operation lost its input device");
        Require(!runtime.RemoteRouteCommitted(operationId + 1, RemoteSource).handled, "stale operation callback was accepted");
        Require(!runtime.RemoteRouteCommitted(operationId, {.session = 8, .movieGeneration = 3}).handled, "wrong-session callback was accepted");

        Require(runtime.RemoteRouteCommitted(operationId, RemoteSource).handled, "exact route commitment was rejected");
        Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "route commitment did not enter PendingRemoteArrival");
        Require(!runtime.InvalidateMapSelection(), "movie invalidation discarded post-route travel");

        auto intermediate = ReadyArrival(operationId, {0x0005E313});
        intermediate.currentSystem = {.starFormId = 0x0005E607, .numericId = 0x00011AF0};
        Require(!runtime.ObserveRemoteArrival(std::move(intermediate)).handled, "intermediate system was mistaken for final arrival");
        Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "intermediate jump discarded the pending operation");
    }

    void TestRemoteLatentMoonAcquiresOnlyFinalCourse()
    {
        ::NavigationRuntime runtime;
        const auto operationId = StartRemote(runtime);
        runtime.RemoteRouteCommitted(operationId, RemoteSource);

        const auto arrived = runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313, 0x00ABCDEF}));
        const auto* press = FindEffect<::PressCruise>(arrived);
        Require(press && press->operationId == operationId, "latent arrival did not emit one correlated Cruise press");
        Require(runtime.CurrentState().phase == ::NavigationPhase::PreparingRemoteTarget, "latent arrival did not enter PreparingRemoteTarget");

        const auto activated = runtime.CruiseChanged(true);
        const auto* request = FindEffect<::RequestCourse>(activated);
        Require(request && request->operationId == operationId, "remote Cruise activation lost operation correlation");
        Require(request->courseId == Chawla().courseId, "remote Cruise requested the waypoint instead of the final moon");
        Require(!runtime.CourseLockChanged(0).handled, "empty delayed lock cancelled the operation");
        Require(runtime.CourseLockChanged(0x0005E313).handled, "ordered parent lock was rejected");
        Require(runtime.CourseLockChanged(0x0005E313).handled, "repeated parent lock was not idempotent");
        Require(runtime.CourseLockChanged(Chawla().courseId).handled, "exact final moon lock was rejected");
        Require(runtime.CurrentState().phase == ::NavigationPhase::CourseLocked, "exact final lock did not complete the operation");
        Require(!runtime.CurrentState().remoteOperation, "completed operation retained asynchronous ownership");
    }

    void TestRemoteAmbiguityUnrelatedLockAndLoadReset()
    {
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);
            Require(runtime.ObserveRemoteArrival(ReadyArrival(operationId, {Chawla().courseId, Chawla().courseId})).handled, "duplicate final rows were not handled");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "duplicate final rows did not fail closed");
        }
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);
            runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313}));
            runtime.CruiseChanged(true);
            Require(runtime.CourseLockChanged(0x00DEAD00).handled, "unrelated nonzero lock was ignored");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "unrelated lock did not cancel the operation");
        }
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);
            Require(runtime.ResetForLoad(), "load did not reset pending remote travel");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "load retained a remote operation");
        }
    }

    void TestRemoteMovieReplacementAndEffectFailureAreCorrelated()
    {
        ::NavigationRuntime runtime;
        const auto operationId = StartRemote(runtime);
        Require(!runtime.RecoverFromEffectFailure(::BeginRemoteRoute {.operationId = operationId + 1, .source = RemoteSource, .destination = Chawla()}), "stale BeginRemoteRoute failure was accepted");
        Require(runtime.InvalidateMapSelection(), "movie replacement did not cancel RoutingRemote");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "movie replacement retained RoutingRemote");

        const auto nextOperationId = StartRemote(runtime);
        Require(nextOperationId > operationId, "operation IDs were reused after cancellation");
        Require(runtime.RecoverFromEffectFailure(::BeginRemoteRoute {.operationId = nextOperationId, .source = RemoteSource, .destination = Chawla()}), "exact BeginRemoteRoute failure was not recovered");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "route effect failure retained the operation");
    }

    void TestRemoteSelectionRejectsUnsafeContexts()
    {
        {
            ::NavigationRuntime runtime;
            const auto result = runtime.SelectDestination(Chawla(), ::SelectionIntent::StartRemoteCruise, true, {.source = RemoteSource});
            Require(!result.handled && !result.effect, "active Cruise started remote routing");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "rejected active-Cruise remote selection changed state");
        }
        {
            ::NavigationRuntime runtime;
            const auto result = runtime.SelectDestination(Chawla(), ::SelectionIntent::StartRemoteCruise, false, {.source = {.session = 0, .movieGeneration = RemoteSource.movieGeneration}});
            Require(!result.handled && !result.effect, "zero-session remote source was accepted");
        }
        {
            ::NavigationRuntime runtime;
            const auto result = runtime.SelectDestination(Chawla(), ::SelectionIntent::StartRemoteCruise, false, {.source = {.session = RemoteSource.session, .movieGeneration = 0}});
            Require(!result.handled && !result.effect, "zero-generation remote source was accepted");
        }
        {
            ::NavigationRuntime runtime;
            auto station = Jemison();
            station.kind = ::DestinationKind::Station;
            const auto result = runtime.SelectDestination(station, ::SelectionIntent::StartRemoteCruise, false, {.source = RemoteSource});
            Require(!result.handled && !result.effect, "station started remote routing");
        }
    }

    void TestRemoteFailureCancellationAndLoadResetAreCorrelated()
    {
        ::NavigationRuntime runtime;
        auto operationId = StartRemote(runtime);

        Require(!runtime.RemoteRouteFailed(operationId + 1, RemoteSource).handled, "stale remote-route failure was accepted");
        Require(!runtime.RemoteRouteFailed(operationId, {.session = 8, .movieGeneration = 3}).handled, "wrong-source remote-route failure was accepted");
        Require(runtime.RemoteRouteFailed(operationId, RemoteSource).handled, "exact remote-route failure was rejected");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "remote-route failure retained navigation state");

        operationId = StartRemote(runtime);
        Require(!runtime.CancelRemoteOperation(operationId + 1), "stale cancellation was accepted");
        Require(runtime.CancelRemoteOperation(operationId), "exact cancellation was rejected");
        Require(!runtime.CancelRemoteOperation(operationId), "repeated cancellation was accepted");

        operationId = StartRemote(runtime);
        Require(runtime.InvalidateRemoteFlight(), "remote-flight invalidation was rejected");
        Require(!runtime.InvalidateRemoteFlight(), "repeated remote-flight invalidation was accepted");

        Require(!runtime.ResetForLoad(), "idle load reset reported a change");
        runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);
        runtime.MapClosed();
        Require(runtime.ResetForLoad(), "load did not clear a stable destination");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Idle && !runtime.CurrentState().destination, "load retained stable navigation state");
    }

    void TestRemoteArrivalRequiresEveryFreshTravelProof()
    {
        ::NavigationRuntime runtime;
        const auto operationId = StartRemote(runtime);
        runtime.RemoteRouteCommitted(operationId, RemoteSource);

        auto observation = ReadyArrival(operationId, {0x0005E313});
        observation.operationId++;
        Require(!runtime.ObserveRemoteArrival(observation).handled, "wrong operation arrival was accepted");

        observation = ReadyArrival(operationId, {0x0005E313});
        observation.mapClosed = false;
        Require(!runtime.ObserveRemoteArrival(observation).handled, "arrival without map close was accepted");

        observation = ReadyArrival(operationId, {0x0005E313});
        observation.loadingMenuClosed = false;
        Require(!runtime.ObserveRemoteArrival(observation).handled, "arrival with loading menu open was accepted");

        observation = ReadyArrival(operationId, {0x0005E313});
        observation.completedPlayerJump = false;
        Require(!runtime.ObserveRemoteArrival(observation).handled, "incomplete player jump was accepted");

        observation = ReadyArrival(operationId, {0x0005E313});
        observation.settledFlight = false;
        Require(!runtime.ObserveRemoteArrival(observation).handled, "unsettled flight was accepted");

        observation = ReadyArrival(operationId, {0x0005E313});
        observation.flying = false;
        Require(!runtime.ObserveRemoteArrival(observation).handled, "non-flight arrival was accepted");

        observation = ReadyArrival(operationId, {0x0005E313});
        observation.currentBodyId = 0;
        Require(!runtime.ObserveRemoteArrival(observation).handled, "arrival without a current body was accepted");

        observation = ReadyArrival(operationId, {0x0005E313});
        observation.freshHudPublication = false;
        Require(!runtime.ObserveRemoteArrival(observation).handled, "stale HUD arrival was accepted");

        observation = ReadyArrival(operationId, {0x0005E313});
        observation.currentSystem = {.starFormId = 0x0005E607, .numericId = 0x00011AF0};
        Require(!runtime.ObserveRemoteArrival(observation).handled, "wrong-system arrival was accepted");

        Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "rejected travel proof discarded the owned operation");
        Require(runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313})).handled, "complete fresh travel proof was rejected");
    }

    void TestRemoteArrivalAtSelectedBodySkipsCruiseAndHudProofs()
    {
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);

            auto premature = ReadyArrival(operationId, {});
            premature.currentBodyId = Chawla().targetId;
            premature.completedPlayerJump = false;
            premature.freshHudPublication = false;
            premature.courseRowsComplete = false;
            Require(!runtime.ObserveRemoteArrival(premature).handled, "selected-body arrival bypassed completed-travel proof");
            Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "premature selected-body arrival discarded remote ownership");

            premature.completedPlayerJump = true;
            premature.currentSystem = {.starFormId = 0x0005E607, .numericId = 0x00011AF0};
            Require(!runtime.ObserveRemoteArrival(premature).handled, "selected-body arrival bypassed exact-system proof");
            Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "wrong-system selected-body arrival discarded remote ownership");

            premature.currentSystem = Chawla().system;
            const auto arrived = runtime.ObserveRemoteArrival(std::move(premature));
            Require(arrived.handled && !arrived.effect, "selected-body arrival emitted an engine effect without HUD proof");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "selected-body arrival did not complete navigation");
            Require(!runtime.CurrentState().destination && !runtime.CurrentState().remoteOperation, "selected-body arrival retained navigation ownership");
        }

        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);

            auto overflowed = ReadyArrival(operationId, {Chawla().courseId, Chawla().courseId});
            overflowed.currentBodyId = Chawla().targetId;
            overflowed.courseRowsComplete = false;
            const auto arrived = runtime.ObserveRemoteArrival(std::move(overflowed));
            Require(arrived.handled && !arrived.effect, "selected-body arrival did not take precedence over incomplete HUD rows");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "selected-body arrival with HUD overflow did not complete navigation");
        }
    }

    void TestReplacementJumpCompletionIsExplicitArrivalProof()
    {
        ::NavigationRuntime runtime;
        const auto operationId = StartRemote(runtime);
        runtime.RemoteRouteCommitted(operationId, RemoteSource);

        auto observation = ReadyArrival(operationId, {0x0005E313});
        observation.completedPlayerJump = false;
        observation.completedReplacementJump = true;
        const auto arrived = runtime.ObserveRemoteArrival(std::move(observation));

        const auto* press = FindEffect<::PressCruise>(arrived);
        Require(press && press->operationId == operationId, "completed replacement jump did not emit one correlated Cruise press");
        Require(runtime.CurrentState().phase == ::NavigationPhase::PreparingRemoteTarget, "completed replacement jump did not enter PreparingRemoteTarget");
    }

    void TestIncompleteHudRowsFailOnlyAtProvenFinalArrival()
    {
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);

            auto premature = ReadyArrival(operationId, {0x0005E313});
            premature.completedPlayerJump = false;
            premature.courseRowsComplete = false;
            Require(!runtime.ObserveRemoteArrival(std::move(premature)).handled, "pre-arrival HUD overflow cancelled remote travel");
            Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "pre-arrival HUD overflow discarded remote ownership");

            auto intermediate = ReadyArrival(operationId, {0x0005E313});
            intermediate.currentSystem = {.starFormId = 0x0005E607, .numericId = 0x00011AF0};
            intermediate.courseRowsComplete = false;
            Require(!runtime.ObserveRemoteArrival(std::move(intermediate)).handled, "intermediate-system HUD overflow cancelled remote travel");
            Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "intermediate-system HUD overflow discarded remote ownership");

            Require(runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313})).handled, "complete final-system HUD rows were rejected after earlier overflow");
            Require(runtime.CurrentState().phase == ::NavigationPhase::PreparingRemoteTarget, "complete final-system HUD rows did not preserve the remote lifecycle after earlier overflow");
        }

        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);

            auto finalArrival = ReadyArrival(operationId, {0x0005E313});
            finalArrival.courseRowsComplete = false;
            Require(runtime.ObserveRemoteArrival(std::move(finalArrival)).handled, "proven final-system HUD overflow was ignored");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "proven final-system HUD overflow did not fail closed");
        }
    }

    void TestRemoteWaypointEvidenceMustBeUniqueAndOrdered()
    {
        auto destination = Chawla();
        destination.remotePlan.allowedWaypointIds = {0x0005E313, 0x0005E314};

        const auto start = [&](::NavigationRuntime& runtime) {
            const auto selected = runtime.SelectDestination(destination, ::SelectionIntent::StartRemoteCruise, false, {.source = RemoteSource});
            const auto* begin = FindEffect<::BeginRemoteRoute>(selected);
            Require(begin != nullptr, "custom remote route did not start");
            Require(runtime.RemoteRouteCommitted(begin->operationId, RemoteSource).handled, "custom remote route did not commit");
            return begin->operationId;
        };

        {
            ::NavigationRuntime runtime;
            const auto operationId = start(runtime);
            Require(runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313, 0x0005E313})).handled, "duplicate waypoint evidence was not handled");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "duplicate waypoint evidence did not fail closed");
        }
        {
            ::NavigationRuntime runtime;
            const auto operationId = start(runtime);
            Require(!runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E314})).handled, "non-first waypoint was accepted as arrival proof");
            Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "out-of-order arrival proof discarded the operation");
        }
        {
            ::NavigationRuntime runtime;
            const auto operationId = start(runtime);
            Require(!runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x00ABCDEF})).handled, "unrelated HUD row was accepted as arrival proof");
            Require(runtime.CurrentState().phase == ::NavigationPhase::PendingRemoteArrival, "unrelated HUD row discarded the operation");
        }
    }

    void TestLocalCallbacksArePhaseSensitiveAndIdempotent()
    {
        ::NavigationRuntime runtime;
        Require(!runtime.MapClosed().handled, "idle map close was handled");
        Require(!runtime.CruiseChanged(true).handled, "idle Cruise activation was handled");
        Require(!runtime.CourseLockChanged(Jemison().courseId).handled, "idle course lock was handled");

        runtime.SelectDestination(Jemison(), ::SelectionIntent::Mark, false);
        runtime.MapClosed();
        Require(!runtime.MapClosed().handled, "duplicate map close was handled");

        Require(runtime.CourseLockChanged(Jemison().courseId).handled, "exact lock did not promote a retained mark");
        Require(runtime.CurrentState().phase == ::NavigationPhase::CourseLocked, "retained mark did not become course-locked");
        Require(runtime.CruiseChanged(false).handled, "Cruise exit did not demote a local course lock");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "Cruise exit did not retain the mark");
        Require(!runtime.CruiseChanged(false).handled, "repeated Cruise exit changed a stable mark");

        runtime.CruiseChanged(true);
        Require(runtime.CurrentState().phase == ::NavigationPhase::AwaitingCourseLock, "Cruise activation did not restore course wait");
        Require(runtime.CruiseChanged(false).handled, "Cruise exit did not cancel local course wait");
        Require(runtime.CurrentState().phase == ::NavigationPhase::Marked, "course-wait cancellation did not retain the mark");
    }

    void TestRemoteCruiseExitTimeoutIsPhaseAndOperationBound()
    {
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);
            Require(!runtime.RemoteCruiseExitTimedOut(operationId), "arrival-phase timeout was accepted as Cruise-exit timeout");
        }
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);
            runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313}));
            Require(!runtime.RemoteCruiseExitTimedOut(operationId + 1), "stale remote Cruise timeout was accepted");
            Require(runtime.RemoteCruiseExitTimedOut(operationId), "preparing remote Cruise timeout was rejected");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "preparing timeout retained the remote operation");
        }
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);
            runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313}));
            runtime.CruiseChanged(true);
            Require(runtime.RemoteCruiseExitTimedOut(operationId), "remote course-wait timeout was rejected");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "remote course-wait timeout retained the operation");
        }
    }

    void TestRemoteEffectFailuresRequireExactOperation()
    {
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);
            const auto arrived = runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313}));
            const auto* press = FindEffect<::PressCruise>(arrived);
            Require(press != nullptr, "remote arrival did not emit Cruise press");
            Require(!runtime.RecoverFromEffectFailure(::PressCruise {.operationId = operationId + 1}), "wrong remote Cruise failure was accepted");
            Require(runtime.RecoverFromEffectFailure(*press), "exact remote Cruise failure was rejected");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "remote Cruise failure retained operation state");
        }
        {
            ::NavigationRuntime runtime;
            const auto operationId = StartRemote(runtime);
            runtime.RemoteRouteCommitted(operationId, RemoteSource);
            runtime.ObserveRemoteArrival(ReadyArrival(operationId, {0x0005E313}));
            const auto request = runtime.CruiseChanged(true);
            const auto* course = FindEffect<::RequestCourse>(request);
            Require(course != nullptr, "remote activation did not emit course request");
            Require(!runtime.RecoverFromEffectFailure(::RequestCourse {.courseId = course->courseId}), "uncorrelated remote course failure was accepted");
            Require(
                !runtime.RecoverFromEffectFailure(
                    ::RequestCourse {
                        .courseId = course->courseId,
                        .operationId = operationId + 1,
                    }
                ),
                "wrong remote course operation was accepted"
            );
            Require(runtime.RecoverFromEffectFailure(*course), "exact remote course failure was rejected");
            Require(runtime.CurrentState().phase == ::NavigationPhase::Idle, "remote course failure retained operation state");
        }
    }

    void TestRemoteOperationValidityRequiresEveryOwnedIdentity()
    {
        auto operation = ::RemoteCruiseOperation {
            .id = 1,
            .source = RemoteSource,
            .inputDevice = ::NavigationInputDevice::KeyboardMouse,
            .destination = Chawla(),
        };
        Require(operation.IsValid(), "complete remote operation was invalid");
        Require(RemoteSource.IsValid(), "complete remote source was invalid");

        auto changed = operation;
        changed.id = 0;
        Require(!changed.IsValid(), "zero operation ID was accepted");

        changed = operation;
        changed.source.session = 0;
        Require(!changed.IsValid(), "zero source session was accepted");

        changed = operation;
        changed.source.movieGeneration = 0;
        Require(!changed.IsValid(), "zero movie generation was accepted");

        changed = operation;
        changed.destination.targetId = 0;
        Require(!changed.IsValid(), "invalid destination was accepted by remote operation");

        changed = operation;
        changed.destination.kind = ::DestinationKind::Station;
        Require(!changed.IsValid(), "station was accepted by remote operation");
    }

    void RunTests()
    {
        TestTapMarksDestination();
        TestSameDestinationTogglesOff();
        TestDestinationReplacement();
        TestMapSelectionInvalidationCancelsIncompleteClose();
        TestMapSelectionInvalidationPreservesPostMapWork();
        TestCompletedHoldUsesExactCourseLock();
        TestAlreadyCruisingSkipsCruisePress();
        TestInvalidDestinationFailsClosed();
        TestSolDestinationIsValid();
        TestCloseMapFailureAbandonsIncompleteSelection();
        TestCruisePressFailureFallsBackToMark();
        TestOnlyExactCourseRequestFailureRecovers();
        TestFailureOutsideOwningPhaseIsIgnored();
        TestRemoteOperationCorrelationAndIntermediatePreservation();
        TestRemoteLatentMoonAcquiresOnlyFinalCourse();
        TestRemoteAmbiguityUnrelatedLockAndLoadReset();
        TestRemoteMovieReplacementAndEffectFailureAreCorrelated();
        TestRemoteSelectionRejectsUnsafeContexts();
        TestRemoteFailureCancellationAndLoadResetAreCorrelated();
        TestRemoteArrivalRequiresEveryFreshTravelProof();
        TestRemoteArrivalAtSelectedBodySkipsCruiseAndHudProofs();
        TestReplacementJumpCompletionIsExplicitArrivalProof();
        TestIncompleteHudRowsFailOnlyAtProvenFinalArrival();
        TestRemoteWaypointEvidenceMustBeUniqueAndOrdered();
        TestLocalCallbacksArePhaseSensitiveAndIdempotent();
        TestRemoteCruiseExitTimeoutIsPhaseAndOperationBound();
        TestRemoteEffectFailuresRequireExactOperation();
        TestRemoteOperationValidityRequiresEveryOwnedIdentity();
    }
}

void RunNavigationRuntimeTests()
{
    RunTests();
}
