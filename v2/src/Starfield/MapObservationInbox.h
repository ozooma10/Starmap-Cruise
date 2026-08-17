#pragma once

#include "Map/MapSessionState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

class MapObservationInbox final
{
public:
    static constexpr std::size_t MaxLifecycleObservations = 16;

    struct LifecycleObservation
    {
        bool opening {false};
        MapSessionIdentity identity;
    };

    struct ViewObservation
    {
        MapSessionIdentity identity;
        MapView view {MapView::Unknown};
    };

    struct Observations
    {
        bool movieCreated {false};
        std::uint32_t movieGeneration {0};
        std::int64_t movieBornTicks {0};

        std::array<LifecycleObservation, MaxLifecycleObservations> lifecycle;
        std::size_t lifecycleCount {0};
        bool lifecycleOverflowed {false};

        std::optional<ViewObservation> view;
    };

    void RecordMovieCreated(std::int64_t bornTicks);
    void RecordLifecycle(bool opening);
    void RecordView(const MapSessionIdentity& identity, MapView view);
    Observations Drain();

private:
    std::mutex m_mutex;

    bool m_movieCreated {false};
    std::uint32_t m_movieGeneration {0};
    std::int64_t m_movieBornTicks {0};

    std::uint32_t m_session {0};
    MapSessionIdentity m_currentIdentity;

    std::array<LifecycleObservation, MaxLifecycleObservations> m_lifecycle;
    std::size_t m_lifecycleCount {0};
    bool m_lifecycleOverflowed {false};

    std::optional<ViewObservation> m_view;
};
