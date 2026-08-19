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
        Require(protocol.Tick(GalaxyInput(), startMs + 10).command ==
                RemoteRouteProtocol::Command::None,
            "galaxy root transition emitted an unexpected command");
        auto selection = GalaxyInput();
        Require(protocol.Tick(selection, startMs + 20).command ==
                RemoteRouteProtocol::Command::InvokeSelector,
            "selection phase did not request the native selector");
        Require(protocol.SelectorCompleted(true, true, startMs + 20),
            "exact selector completion was rejected");
        selection.selectedReadable = true;
        selection.selectedSystem = DestinationRoot;
        selection.setCourseGateResolved = true;
        selection.setCourseReady = true;
        Require(protocol.Tick(selection, startMs + 30).command ==
                RemoteRouteProtocol::Command::DispatchSetCourse,
            "selection phase did not request stock Set Course");
        Require(protocol.SetCourseCompleted(true, true, startMs + 30),
            "consumed Set Course handoff was rejected");
        Require(protocol.CurrentPhase() == RemoteRouteProtocol::Phase::AwaitRoute,
            "Set Course did not enter AwaitRoute");
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
        Require(protocol.Tick(ExactRouteInput(), 200).command ==
                RemoteRouteProtocol::Command::None,
            "route readiness bypassed its dwell");
        Require(protocol.Tick(ExactRouteInput(), 701).command ==
                RemoteRouteProtocol::Command::InvokeExecute,
            "continuous exact route did not request Execute");
        Require(protocol.ExecuteStarted(12, 701), "Execute start was rejected");
        Require(protocol.CurrentPhase() == RemoteRouteProtocol::Phase::AwaitExecuteClose,
            "Execute did not enter AwaitExecuteClose");
        Require(protocol.MapClosed(true, 13) == RemoteRouteProtocol::CloseResult::Committed,
            "native Execute plus matching close did not commit");
    }

    void TestWrongAndPreexistingRoutesFailClosed()
    {
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            auto wrong = ExactRouteInput();
            wrong.endpointIdentity = RemoteRouteProtocol::EndpointIdentity::WrongSystem;
            Require(protocol.Tick(wrong, 200).failed,
                "wrong-system structured endpoint was accepted");
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
            selection.setCourseGateResolved = true;
            selection.setCourseReady = true;
            protocol.Tick(selection, 130);
            protocol.SetCourseCompleted(true, true, 130);
            Require(protocol.Tick(ExactRouteInput(), 5140).failed,
                "unchanged pre-existing endpoint was treated as a new owned route");
        }
    }

    void TestFocusSuspendsDeadlineAndClearsReadinessDwell()
    {
        RemoteRouteProtocol protocol;
        ReachAwaitRoute(protocol);
        Require(protocol.Tick(ExactRouteInput(), 200).command ==
                RemoteRouteProtocol::Command::None,
            "focus test did not arm readiness");

        auto background = ExactRouteInput();
        background.foreground = false;
        Require(!protocol.Tick(background, 5000).failed,
            "background time consumed the route deadline");
        Require(protocol.Tick(ExactRouteInput(), 5001).command ==
                RemoteRouteProtocol::Command::None,
            "foreground restoration retained stale readiness dwell");
        Require(protocol.Tick(ExactRouteInput(), 5502).command ==
                RemoteRouteProtocol::Command::InvokeExecute,
            "fresh foreground dwell did not reach Execute");

        RemoteRouteProtocol galaxyWait;
        galaxyWait.Begin(DestinationRoot, 0, 100);
        auto backgroundGalaxy = GalaxyInput();
        backgroundGalaxy.foreground = false;
        Require(!galaxyWait.Tick(backgroundGalaxy, 10100).failed,
            "background interval consumed the galaxy-stage deadline");
        Require(!galaxyWait.Tick(GalaxyInput(), 10101).failed &&
                galaxyWait.CurrentPhase() == RemoteRouteProtocol::Phase::EstablishSelection,
            "foreground restoration could not complete the paused galaxy stage");
    }

    void TestExecuteRequiresEventAndTimelyMatchingClose()
    {
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            protocol.Tick(ExactRouteInput(), 200);
            protocol.Tick(ExactRouteInput(), 701);
            protocol.ExecuteStarted(20, 701);
            Require(protocol.MapClosed(true, 20) == RemoteRouteProtocol::CloseResult::Failed,
                "map close without a newer native Execute event committed");
        }
        {
            RemoteRouteProtocol protocol;
            ReachAwaitRoute(protocol);
            protocol.Tick(ExactRouteInput(), 200);
            protocol.Tick(ExactRouteInput(), 701);
            protocol.ExecuteStarted(20, 701);
            RemoteRouteProtocol::TickInput waiting;
            Require(protocol.Tick(waiting, 2702).failed,
                "Execute without a timely close acknowledgement did not fail");
        }
    }

    void TestCorrelationAndOwnershipFailuresStopTheProtocol()
    {
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            auto wrongSession = GalaxyInput();
            wrongSession.sourceMatches = false;
            Require(protocol.Tick(wrongSession, 110).failed,
                "wrong map session was accepted");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            protocol.Tick(GalaxyInput(), 110);
            protocol.Tick(GalaxyInput(), 120);
            Require(!protocol.SelectorCompleted(true, false, 120) &&
                    protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Failed,
                "wrong selector readback did not fail");
        }
        {
            RemoteRouteProtocol protocol;
            protocol.Begin(DestinationRoot, 0, 100);
            Require(protocol.MovieReplaced() &&
                    protocol.CurrentPhase() == RemoteRouteProtocol::Phase::Failed,
                "movie replacement did not fail the active protocol");
        }
    }
}

void RunRemoteRouteProtocolTests()
{
    TestHappyPathRequiresEveryCoarsePhase();
    TestWrongAndPreexistingRoutesFailClosed();
    TestFocusSuspendsDeadlineAndClearsReadinessDwell();
    TestExecuteRequiresEventAndTimelyMatchingClose();
    TestCorrelationAndOwnershipFailuresStopTheProtocol();
}
