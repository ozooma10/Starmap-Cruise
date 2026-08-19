#pragma once

#include "Map/MapSessionState.h"

#include <cstdint>
#include <string_view>

class RemoteRouteProtocol final
{
public:
    enum class Phase : std::uint8_t
    {
        Idle,
        AwaitGalaxy,
        EstablishSelection,
        AwaitRoute,
        AwaitExecuteClose,
        Failed,
    };

    enum class EndpointIdentity : std::uint8_t
    {
        Unavailable,
        ExactDestination,
        WrongSystem,
    };

    enum class Command : std::uint8_t
    {
        None,
        InvokeSelector,
        DispatchSetCourse,
        InvokeExecute,
    };

    struct TickInput
    {
        bool foreground {true};
        bool sourceMatches {true};
        bool mapReadable {false};
        MapView view {MapView::Unknown};
        FormID focusedRoot {0};
        bool selectedReadable {false};
        FormID selectedSystem {0};
        bool setCourseGateResolved {false};
        bool setCourseReady {false};
        bool routeReadable {false};
        FormID routeEndpoint {0};
        EndpointIdentity endpointIdentity {EndpointIdentity::Unavailable};
        bool executeGateResolved {false};
        bool executeReady {false};
    };

    struct Decision
    {
        Command command {Command::None};
        bool failed {false};
        std::string_view reason;
    };

    enum class CloseResult : std::uint8_t
    {
        Ignored,
        Committed,
        Failed,
    };

    void Begin(FormID destinationRoot, FormID preexistingEndpoint, std::int64_t nowMs);
    Decision Tick(const TickInput& input, std::int64_t nowMs);
    bool SelectorCompleted(bool invoked, bool exactReadback, std::int64_t nowMs);
    bool SetCourseCompleted(bool dispatched, bool ownershipConsumed, std::int64_t nowMs);
    bool ExecuteStarted(std::uint64_t executeEventFloor, std::int64_t nowMs);
    CloseResult MapClosed(bool sourceMatches, std::uint64_t executeEventCount);
    bool MovieReplaced();
    void Reset();

    bool Active() const;
    Phase CurrentPhase() const;
    std::string_view FailureReason() const;

private:
    void AdvanceClock(bool foreground, std::int64_t nowMs);
    void StartStage(Phase phase, std::int64_t nowMs);
    Decision Fail(std::string_view reason);

    Phase m_phase {Phase::Idle};
    FormID m_destinationRoot {0};
    FormID m_preexistingEndpoint {0};
    bool m_selectorCompleted {false};
    std::uint64_t m_executeEventFloor {0};
    std::int64_t m_lastTickMs {0};
    std::int64_t m_stageElapsedMs {0};
    std::int64_t m_readySinceMs {-1};
    bool m_wasForeground {true};
    std::string_view m_failureReason;
};
