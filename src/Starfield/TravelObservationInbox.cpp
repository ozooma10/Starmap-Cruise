#include "Starfield/TravelObservationInbox.h"
#include "Domain/NonzeroCounter.h"

#include <chrono>

namespace
{
    std::int64_t NowTicks() noexcept
    {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }
}

void TravelObservationInbox::RecordGravJump(std::uint32_t state, FormID destinationId) noexcept
{
    Push({
        .kind = Kind::GravJump,
        .ticks = NowTicks(),
        .gravState = state,
        .destinationId = destinationId,
    });
}

void TravelObservationInbox::RecordLoadingMenu(bool opening) noexcept
{
    Push({
        .kind = Kind::LoadingMenu,
        .ticks = NowTicks(),
        .opening = opening,
    });
}

void TravelObservationInbox::RecordLoadGame() noexcept
{
    Push({.kind = Kind::LoadGame, .ticks = NowTicks()});
}

TravelObservationInbox::Observations TravelObservationInbox::Drain()
{
    std::lock_guard lock {m_mutex};
    Observations result {
        .count = m_count,
        .overflowed = m_overflowed || m_producerFaulted.exchange(false, std::memory_order_acq_rel),
    };
    for (std::size_t index = 0; index < m_count; ++index) {
        result.values[index] = m_values[index];
    }
    m_count = 0;
    m_overflowed = false;
    return result;
}

void TravelObservationInbox::Push(Observation observation) noexcept
{
    try {
        std::lock_guard lock {m_mutex};
        if (m_overflowed) {
            return;
        }
        if (m_count == m_values.size()) {
            m_count = 0;
            m_overflowed = true;
            return;
        }
        m_nextSequence = CFS::AdvanceNonzeroCounter(m_nextSequence);
        observation.sequence = m_nextSequence;
        m_values[m_count++] = observation;
    } catch (...) {
        m_producerFaulted.store(true, std::memory_order_release);
    }
}
