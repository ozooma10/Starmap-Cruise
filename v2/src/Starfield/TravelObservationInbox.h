#pragma once

#include "Domain/Destination.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

class TravelObservationInbox final
{
public:
    static constexpr std::size_t MaxObservations = 32;

    enum class Kind : std::uint8_t
    {
        GravJump,
        LoadingMenu,
        LoadGame,
    };

    struct Observation
    {
        Kind kind {Kind::GravJump};
        std::uint64_t sequence {0};
        std::int64_t ticks {0};
        std::uint32_t gravState {0};
        FormID destinationId {0};
        bool opening {false};
    };

    struct Observations
    {
        std::array<Observation, MaxObservations> values;
        std::size_t count {0};
        bool overflowed {false};
    };

    void RecordGravJump(std::uint32_t state, FormID destinationId) noexcept;
    void RecordLoadingMenu(bool opening) noexcept;
    void RecordLoadGame() noexcept;
    Observations Drain();

private:
    void Push(Observation observation) noexcept;

    std::mutex m_mutex;
    std::array<Observation, MaxObservations> m_values;
    std::size_t m_count {0};
    bool m_overflowed {false};
    std::uint64_t m_nextSequence {0};
    std::atomic_bool m_producerFaulted {false};
};
