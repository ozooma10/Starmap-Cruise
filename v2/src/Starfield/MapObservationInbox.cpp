#include "Starfield/MapObservationInbox.h"

void MapObservationInbox::RecordMovieCreated(std::int64_t bornTicks)
{
    std::lock_guard lock {m_mutex};

    m_movieGeneration++;
    if (m_movieGeneration == 0) {
        m_movieGeneration++;
    }

    m_movieCreated = true;
    m_movieBornTicks = bornTicks;
    m_session = 0;
    m_currentIdentity = {};
    m_view.reset();
}

void MapObservationInbox::RecordLifecycle(bool opening)
{
    std::lock_guard lock {m_mutex};

    if (m_lifecycleOverflowed) {
        return;
    }

    if (m_lifecycleCount == m_lifecycle.size()) {
        m_lifecycleCount = 0;
        m_lifecycleOverflowed = true;
        m_currentIdentity = {};
        m_view.reset();
        return;
    }

    auto identity = m_currentIdentity;

    if (opening) {
        m_session++;
        if (m_session == 0) {
            m_session++;
        }

        identity = {
            .session = m_session,
            .generation = m_movieGeneration,
        };
        m_currentIdentity = identity;
    } else {
        m_currentIdentity = {};
    }

    m_view.reset();
    m_lifecycle[m_lifecycleCount++] = {
        .opening = opening,
        .identity = identity,
    };
}

void MapObservationInbox::RecordView(const MapSessionIdentity& identity, MapView view)
{
    std::lock_guard lock {m_mutex};

    if (!identity.IsValid() || identity != m_currentIdentity) {
        return;
    }

    if (!m_view) {
        m_view = ViewObservation {
            .identity = identity,
            .view = view,
        };
    } else if (m_view->view != view) {
        m_view->view = MapView::Unknown;
    }
}

MapObservationInbox::Observations MapObservationInbox::Drain()
{
    std::lock_guard lock {m_mutex};

    Observations observations {
        .movieCreated = m_movieCreated,
        .movieGeneration = m_movieGeneration,
        .movieBornTicks = m_movieBornTicks,
        .lifecycleCount = m_lifecycleCount,
        .lifecycleOverflowed = m_lifecycleOverflowed,
        .view = m_view,
    };

    for (std::size_t index = 0; index < m_lifecycleCount; ++index) {
        observations.lifecycle[index] = m_lifecycle[index];
    }

    m_movieCreated = false;
    m_lifecycleCount = 0;
    m_lifecycleOverflowed = false;
    m_view.reset();

    return observations;
}
