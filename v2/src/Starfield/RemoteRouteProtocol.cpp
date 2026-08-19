#include "Starfield/RemoteRouteProtocol.h"

namespace
{
    constexpr std::int64_t StageTimeoutMs = 5000;
    constexpr std::int64_t ExecuteReadyDwellMs = 500;
    constexpr std::int64_t ExecuteCloseTimeoutMs = 2000;
}

void RemoteRouteProtocol::Begin(FormID destinationRoot, FormID preexistingEndpoint, std::int64_t nowMs)
{
    Reset();
    if (destinationRoot == 0) {
        Fail("destination system root is unavailable");
        return;
    }
    m_destinationRoot = destinationRoot;
    m_preexistingEndpoint = preexistingEndpoint;
    StartStage(Phase::AwaitGalaxy, nowMs);
}

RemoteRouteProtocol::Decision RemoteRouteProtocol::Tick(const TickInput& input, std::int64_t nowMs)
{
    if (!Active()) {
        return {.failed = m_phase == Phase::Failed, .reason = m_failureReason};
    }
    if (!input.sourceMatches) {
        return Fail("active Starmap session changed");
    }

    AdvanceClock(input.foreground, nowMs);
    if (!input.foreground) {
        return {};
    }

    if (m_phase == Phase::AwaitExecuteClose) {
        if (m_stageElapsedMs > ExecuteCloseTimeoutMs) {
            return Fail("stock Execute produced no matching Starmap close acknowledgement");
        }
        return {};
    }

    if (!input.mapReadable) {
        if (m_stageElapsedMs > StageTimeoutMs) {
            return Fail("live Starmap state was unavailable before the stage deadline");
        }
        return {};
    }

    if (m_phase == Phase::AwaitGalaxy) {
        if (input.view == MapView::Galaxy && input.focusedRoot == m_destinationRoot) {
            StartStage(Phase::EstablishSelection, nowMs);
            return {};
        }
        if (m_stageElapsedMs > StageTimeoutMs) {
            return Fail("stock Back did not reach the captured destination-system root");
        }
        return {};
    }

    if (m_phase == Phase::EstablishSelection) {
        if (!m_selectorCompleted) {
            return {.command = Command::InvokeSelector};
        }
        if (!input.selectedReadable || input.selectedSystem != m_destinationRoot) {
            return Fail("native selected system changed before Set Course");
        }
        if (input.setCourseGateResolved && input.setCourseReady) {
            return {.command = Command::DispatchSetCourse};
        }
        if (m_stageElapsedMs > StageTimeoutMs) {
            return Fail("system-level Set Course never became enabled and visible");
        }
        return {};
    }

    const bool newEndpoint = input.routeReadable && input.routeEndpoint != 0 &&
        input.routeEndpoint != m_preexistingEndpoint;
    if (newEndpoint && input.endpointIdentity == EndpointIdentity::WrongSystem) {
        return Fail("structured route endpoint resolved to the wrong system");
    }
    const bool ready = newEndpoint &&
        input.endpointIdentity == EndpointIdentity::ExactDestination &&
        input.executeGateResolved && input.executeReady;
    if (!ready) {
        m_readySinceMs = -1;
    } else if (m_readySinceMs < 0) {
        m_readySinceMs = m_stageElapsedMs;
    } else if (m_stageElapsedMs - m_readySinceMs >= ExecuteReadyDwellMs) {
        return {.command = Command::InvokeExecute};
    }

    if (m_stageElapsedMs > StageTimeoutMs) {
        return Fail("Set Course did not produce a new exact executable route before the stage deadline");
    }
    return {};
}

bool RemoteRouteProtocol::SelectorCompleted(bool invoked, bool exactReadback, std::int64_t nowMs)
{
    if (m_phase != Phase::EstablishSelection || m_selectorCompleted) {
        return false;
    }
    if (!invoked || !exactReadback) {
        Fail("guarded native system selector readback did not equal captured STDT");
        return false;
    }
    m_selectorCompleted = true;
    m_lastTickMs = nowMs;
    return true;
}

bool RemoteRouteProtocol::SetCourseCompleted(bool dispatched, bool ownershipConsumed, std::int64_t nowMs)
{
    if (m_phase != Phase::EstablishSelection || !m_selectorCompleted) {
        return false;
    }
    if (!dispatched) {
        Fail("stock system-level Set Course dispatch failed");
        return false;
    }
    if (!ownershipConsumed) {
        Fail("stock Set Course did not consume Quick Select ownership");
        return false;
    }
    StartStage(Phase::AwaitRoute, nowMs);
    return true;
}

bool RemoteRouteProtocol::ExecuteStarted(std::uint64_t executeEventFloor, std::int64_t nowMs)
{
    if (m_phase != Phase::AwaitRoute || m_readySinceMs < 0) {
        return false;
    }
    m_executeEventFloor = executeEventFloor;
    StartStage(Phase::AwaitExecuteClose, nowMs);
    return true;
}

RemoteRouteProtocol::CloseResult RemoteRouteProtocol::MapClosed(bool sourceMatches, std::uint64_t executeEventCount)
{
    if (!Active()) {
        return CloseResult::Ignored;
    }
    if (!sourceMatches || m_phase != Phase::AwaitExecuteClose ||
        executeEventCount <= m_executeEventFloor) {
        Fail(m_phase == Phase::AwaitExecuteClose ?
            "Starmap closed without the correlated native Execute event" :
            "Starmap closed before the remote route committed");
        return CloseResult::Failed;
    }
    return CloseResult::Committed;
}

bool RemoteRouteProtocol::MovieReplaced()
{
    if (!Active()) {
        return false;
    }
    Fail("Starmap movie generation was replaced during remote routing");
    return true;
}

void RemoteRouteProtocol::Reset()
{
    m_phase = Phase::Idle;
    m_destinationRoot = 0;
    m_preexistingEndpoint = 0;
    m_selectorCompleted = false;
    m_executeEventFloor = 0;
    m_lastTickMs = 0;
    m_stageElapsedMs = 0;
    m_readySinceMs = -1;
    m_wasForeground = true;
    m_failureReason = {};
}

bool RemoteRouteProtocol::Active() const
{
    return m_phase != Phase::Idle && m_phase != Phase::Failed;
}

RemoteRouteProtocol::Phase RemoteRouteProtocol::CurrentPhase() const
{
    return m_phase;
}

std::string_view RemoteRouteProtocol::FailureReason() const
{
    return m_failureReason;
}

void RemoteRouteProtocol::AdvanceClock(bool foreground, std::int64_t nowMs)
{
    if (m_lastTickMs != 0 && foreground && m_wasForeground &&
        nowMs >= m_lastTickMs) {
        m_stageElapsedMs += nowMs - m_lastTickMs;
    }
    m_lastTickMs = nowMs;
    if (!foreground) {
        m_readySinceMs = -1;
    }
    m_wasForeground = foreground;
}

void RemoteRouteProtocol::StartStage(Phase phase, std::int64_t nowMs)
{
    m_phase = phase;
    m_lastTickMs = nowMs;
    m_stageElapsedMs = 0;
    m_readySinceMs = -1;
    m_wasForeground = true;
}

RemoteRouteProtocol::Decision RemoteRouteProtocol::Fail(std::string_view reason)
{
    m_phase = Phase::Failed;
    m_failureReason = reason;
    return {.failed = true, .reason = reason};
}
