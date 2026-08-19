#include "Starfield/RemoteRouteProtocol.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    constexpr FormID DestinationRoot = 0x0005E60A;
    constexpr FormID Endpoint = 0x0003F5A1;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error {std::string {message}};
        }
    }

    RemoteRouteProtocol::TickInput GalaxyInput()
    {
        return {
            .mapReadable = true,
            .view = MapView::Galaxy,
            .focusedRoot = DestinationRoot,
        };
    }

    void ReachAwaitRoute(RemoteRouteProtocol& protocol, std::int64_t startMs = 100)
    {
        protocol.Begin(DestinationRoot, 0, startMs);
        Require(protocol.Tick(GalaxyInput(), startMs + 10).command == RemoteRouteProtocol::Command::None, "galaxy root transition emitted an unexpected command");
        auto selection = GalaxyInput();
        Require(protocol.Tick(selection, startMs + 20).command == RemoteRouteProtocol::Command::InvokeSelector, "selection phase did not request the native selector");
        Require(protocol.SelectorCompleted(true, true, startMs + 20), "exact selector completion was rejected");
        selection.selectedReadable = true;
        selection.selectedSystem = DestinationRoot;
        Require(protocol.Tick(selection, startMs + 30).command == RemoteRouteProtocol::Command::DispatchSetCourse, "exact cursorless selection did not request stock Set Course");
        Require(protocol.SetCourseCompleted(true, true, startMs + 30), "consumed Set Course handoff was rejected");
        Require(protocol.CurrentPhase() == RemoteRouteProtocol::Phase::AwaitRoute, "Set Course did not enter AwaitRoute");
    }

    RemoteRouteProtocol::TickInput ExactRouteInput()
    {
        return {
            .mapReadable = true,
            .view = MapView::Galaxy,
            .focusedRoot = DestinationRoot,
            .routeReadable = true,
            .routeEndpoint = Endpoint,
            .endpointIdentity = RemoteRouteProtocol::EndpointIdentity::ExactDestination,
            .executeGateResolved = true,
            .executeReady = true,
        };
    }

    void TestHappyPathRequiresEveryCoarsePhase()
    {
        RemoteRouteProtocol protocol;
        ReachAwaitRoute(protocol);
        Require(protocol.Tick(ExactRouteInput(), 200).command == RemoteRouteProtocol::Command::None, "route readiness bypassed its dwell");
        Require(protocol.Tick(ExactRouteInput(), 701).command == RemoteRouteProtocol::Command::InvokeExecute, "continuous exact route did not request Execute");
        Require(protocol.ExecuteStarted(12, 701), "Execute start was rejected");
        Require(protocol.CurrentPhase() == RemoteRouteProtocol::Phase::AwaitExecuteClose, "Execute did not enter AwaitExecuteClose");
        Require(protocol.MapClosed(true, 13) == RemoteRouteProtocol::CloseResult::Committed, "native Execute plus matching close did not commit");
    }

    void TestWrongAndPreexistingRoutesFailClosed()
    {
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            auto wrong = ExactRouteInput();
            wrong.endpointIdentity = RemoteRouteProtocol::EndpointIdentity::WrongSystem;
            Require(protocol.Tick(wrong, 200).failed, "wrong-system structured endpoint was accepted");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, Endpoint, 100);
            protocol.Tick(GalaxyInput(), 110);
            auto selection = GalaxyInput();
            protocol.Tick(selection, 120);
            protocol.SelectorCompleted(true, true, 120);
            selection.selectedReadable = true;
            selection.selectedSystem = DestinationRoot;
            protocol.Tick(selection, 130);
            protocol.SetCourseCompleted(true, true, 130);
            Require(protocol.Tick(ExactRouteInput(), 5140).failed, "unchanged pre-existing endpoint was treated as a new owned route");
        }
    }

    void TestGalaxyGateRequiresTheSystemFormRatherThanTheDisplayedBody()
    {
        RemoteRouteProtocol protocol;
        protocol.Begin(DestinationRoot, 0, 100);

        auto bodyIdentity = GalaxyInput();
        bodyIdentity.focusedRoot = Endpoint;
        Require(!protocol.Tick(bodyIdentity, 110).failed && protocol.CurrentPhase() == RemoteRouteProtocol::Phase::AwaitGalaxy, "a displayed body FormID was mistaken for the destination STDT root");

        Require(!protocol.Tick(GalaxyInput(), 120).failed && protocol.CurrentPhase() == RemoteRouteProtocol::Phase::EstablishSelection, "the exact destination STDT did not complete the galaxy gate");
    }

    void TestFocusSuspendsDeadlineAndClearsReadinessDwell()
    {
        RemoteRouteProtocol protocol;
        ReachAwaitRoute(protocol);
        Require(protocol.Tick(ExactRouteInput(), 200).command == RemoteRouteProtocol::Command::None, "focus test did not arm readiness");

        auto background = ExactRouteInput();
        background.foreground = false;
        Require(!protocol.Tick(background, 5000).failed, "background time consumed the route deadline");
        Require(protocol.Tick(ExactRouteInput(), 5001).command == RemoteRouteProtocol::Command::None, "foreground restoration retained stale readiness dwell");
        Require(protocol.Tick(ExactRouteInput(), 5502).command == RemoteRouteProtocol::Command::InvokeExecute, "fresh foreground dwell did not reach Execute");

        RemoteRouteProtocol galaxyWait;
        galaxyWait.Begin(DestinationRoot, 0, 100);
        auto backgroundGalaxy = GalaxyInput();
        backgroundGalaxy.foreground = false;
        Require(!galaxyWait.Tick(backgroundGalaxy, 10100).failed, "background interval consumed the galaxy-stage deadline");
        Require(!galaxyWait.Tick(GalaxyInput(), 10101).failed && galaxyWait.CurrentPhase() == RemoteRouteProtocol::Phase::EstablishSelection, "foreground restoration could not complete the paused galaxy stage");
    }

    void TestExecuteRequiresEventAndTimelyMatchingClose()
    {
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            protocol.Tick(ExactRouteInput(), 200);
            protocol.Tick(ExactRouteInput(), 701);
            protocol.ExecuteStarted(20, 701);
            Require(protocol.MapClosed(true, 20) == RemoteRouteProtocol::CloseResult::Failed, "map close without a newer native Execute event committed");
        }
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            protocol.Tick(ExactRouteInput(), 200);
            protocol.Tick(ExactRouteInput(), 701);
            protocol.ExecuteStarted(20, 701);
            RemoteRouteProtocol::TickInput waiting;
            Require(protocol.Tick(waiting, 2702).failed, "Execute without a timely close acknowledgement did not fail");
        }
    }

    void TestCorrelationAndOwnershipFailuresStopTheProtocol()
    {
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            auto wrongSession = GalaxyInput();
            wrongSession.sourceMatches = false;
            Require(protocol.Tick(wrongSession, 110).failed, "wrong map session was accepted");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            protocol.Tick(GalaxyInput(), 110);
            protocol.Tick(GalaxyInput(), 120);
            Require(!protocol.SelectorCompleted(true, false, 120) && protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Failed, "wrong selector readback did not fail");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            Require(protocol.MovieReplaced() && protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Failed, "movie replacement did not fail the active protocol");
        }
    }

    void TestInactiveAndInvalidBeginOperationsAreSafe()
    {
        RemoteRouteProtocol protocol;
        Require(!protocol.Active() && protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Idle && protocol.FailureReason().empty(), "fresh protocol was not idle");
        Require(protocol.Tick({}, 100).command == RemoteRouteProtocol::Command::None, "idle tick emitted a command");
        Require(protocol.MapClosed(true, 1) == RemoteRouteProtocol::CloseResult::Ignored, "idle close was not ignored");
        Require(!protocol.MovieReplaced(), "idle movie replacement reported a change");
        Require(!protocol.SelectorCompleted(true, true, 100), "idle selector completion was accepted");
        Require(!protocol.SetCourseCompleted(true, true, 100), "idle Set Course completion was accepted");
        Require(!protocol.ExecuteStarted(1, 100), "idle Execute start was accepted");

        protocol.Begin(0, 0, 100);
        Require(!protocol.Active() && protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Failed, "zero destination root did not fail");
        Require(protocol.FailureReason() == "destination system root is unavailable", "zero destination root produced the wrong reason");
        const auto failedTick = protocol.Tick({}, 110);
        Require(failedTick.failed && failedTick.reason == protocol.FailureReason(), "failed protocol did not retain its failure result");
        Require(protocol.MapClosed(true, 1) == RemoteRouteProtocol::CloseResult::Ignored, "failed protocol accepted map close");
        Require(!protocol.MovieReplaced(), "failed protocol accepted movie replacement");

        protocol.Reset();
        Require(!protocol.Active() && protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Idle && protocol.FailureReason().empty(), "reset did not restore idle protocol state");
    }

    void TestEveryStageTimeoutUsesStrictDeadline()
    {
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            Require(!protocol.Tick({}, 5100).failed, "unreadable map failed at the inclusive stage boundary");
            Require(protocol.Tick({}, 5101).failed, "unreadable map exceeded its deadline without failure");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            auto wrongRoot = GalaxyInput();
            wrongRoot.focusedRoot = Endpoint;
            Require(!protocol.Tick(wrongRoot, 5100).failed, "galaxy gate failed at the inclusive stage boundary");
            Require(protocol.Tick(wrongRoot, 5101).failed, "galaxy gate exceeded its deadline without failure");
        }
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            auto unavailable = ExactRouteInput();
            unavailable.routeReadable = false;
            Require(!protocol.Tick(unavailable, 5130).failed, "route wait failed at the inclusive stage boundary");
            Require(protocol.Tick(unavailable, 5131).failed, "route wait exceeded its deadline without failure");
        }
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            protocol.Tick(ExactRouteInput(), 200);
            protocol.Tick(ExactRouteInput(), 701);
            Require(protocol.ExecuteStarted(10, 701), "timeout setup did not start Execute");
            Require(!protocol.Tick({}, 2701).failed, "Execute close failed at the inclusive deadline");
            Require(protocol.Tick({}, 2702).failed, "Execute close exceeded its deadline without failure");
        }
    }

    void TestSelectorAndSetCourseCallbacksArePhaseBound()
    {
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            protocol.Tick(GalaxyInput(), 110);
            Require(!protocol.SetCourseCompleted(true, true, 120), "Set Course completed before selector ownership");
            Require(!protocol.SelectorCompleted(false, true, 120), "non-invoked selector was accepted");
            Require(protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Failed, "non-invoked selector did not fail closed");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            protocol.Tick(GalaxyInput(), 110);
            Require(protocol.SelectorCompleted(true, true, 120), "valid selector completion was rejected");
            Require(!protocol.SelectorCompleted(true, true, 120), "duplicate selector completion was accepted");

            auto unreadable = GalaxyInput();
            Require(protocol.Tick(unreadable, 130).failed, "unreadable selected system was accepted");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            protocol.Tick(GalaxyInput(), 110);
            protocol.SelectorCompleted(true, true, 120);
            auto wrong = GalaxyInput();
            wrong.selectedReadable = true;
            wrong.selectedSystem = Endpoint;
            Require(protocol.Tick(wrong, 130).failed, "wrong selected system was accepted");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            protocol.Tick(GalaxyInput(), 110);
            protocol.SelectorCompleted(true, true, 120);
            Require(!protocol.SetCourseCompleted(false, true, 130), "failed Set Course dispatch was accepted");
            Require(protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Failed, "failed Set Course dispatch did not stop the protocol");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            protocol.Tick(GalaxyInput(), 110);
            protocol.SelectorCompleted(true, true, 120);
            Require(!protocol.SetCourseCompleted(true, false, 130), "unconsumed Quick Select ownership was accepted");
            Require(protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Failed, "unconsumed Quick Select ownership did not stop the protocol");
        }
    }

    void TestRouteReadinessMustBeExactAndContinuous()
    {
        RemoteRouteProtocol protocol;
        ReachAwaitRoute(protocol);

        auto input = ExactRouteInput();
        input.routeReadable = false;
        Require(protocol.Tick(input, 200).command == RemoteRouteProtocol::Command::None, "unreadable route became executable");

        input = ExactRouteInput();
        input.routeEndpoint = 0;
        Require(protocol.Tick(input, 300).command == RemoteRouteProtocol::Command::None, "zero endpoint became executable");

        input = ExactRouteInput();
        input.endpointIdentity = RemoteRouteProtocol::EndpointIdentity::Unavailable;
        Require(protocol.Tick(input, 400).command == RemoteRouteProtocol::Command::None, "unresolved endpoint became executable");

        input = ExactRouteInput();
        input.executeGateResolved = false;
        Require(protocol.Tick(input, 500).command == RemoteRouteProtocol::Command::None, "unresolved Execute gate became executable");

        input = ExactRouteInput();
        input.executeReady = false;
        Require(protocol.Tick(input, 600).command == RemoteRouteProtocol::Command::None, "disabled Execute gate became executable");

        Require(!protocol.ExecuteStarted(1, 600), "Execute started without any continuous ready observation");
        Require(protocol.Tick(ExactRouteInput(), 700).command == RemoteRouteProtocol::Command::None, "first ready observation bypassed dwell");

        input = ExactRouteInput();
        input.executeReady = false;
        protocol.Tick(input, 1000);
        Require(protocol.Tick(ExactRouteInput(), 1100).command == RemoteRouteProtocol::Command::None, "readiness resumed stale dwell");
        Require(protocol.Tick(ExactRouteInput(), 1600).command == RemoteRouteProtocol::Command::InvokeExecute, "continuous ready dwell did not become executable");
    }

    void TestMapCloseRequiresPhaseSourceAndNewExecuteEvent()
    {
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            Require(protocol.MapClosed(true, 1) == RemoteRouteProtocol::CloseResult::Failed, "map close before Execute did not fail");
            Require(protocol.FailureReason() == "Starmap closed before the remote route committed", "pre-Execute close produced the wrong reason");
        }
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            protocol.Tick(ExactRouteInput(), 200);
            protocol.Tick(ExactRouteInput(), 701);
            Require(protocol.ExecuteStarted(10, 701), "correlation setup did not start Execute");
            Require(protocol.MapClosed(false, 11) == RemoteRouteProtocol::CloseResult::Failed, "wrong-source close committed");
            Require(protocol.FailureReason() == "Starmap closed without the correlated native Execute event", "wrong-source close produced the wrong reason");
        }
    }
}

void RunRemoteRouteProtocolTests()
{
    TestHappyPathRequiresEveryCoarsePhase();
    TestWrongAndPreexistingRoutesFailClosed();
    TestGalaxyGateRequiresTheSystemFormRatherThanTheDisplayedBody();
    TestFocusSuspendsDeadlineAndClearsReadinessDwell();
    TestExecuteRequiresEventAndTimelyMatchingClose();
    TestCorrelationAndOwnershipFailuresStopTheProtocol();
    TestInactiveAndInvalidBeginOperationsAreSafe();
    TestEveryStageTimeoutUsesStrictDeadline();
    TestSelectorAndSetCourseCallbacksArePhaseBound();
    TestRouteReadinessMustBeExactAndContinuous();
    TestMapCloseRequiresPhaseSourceAndNewExecuteEvent();
}
