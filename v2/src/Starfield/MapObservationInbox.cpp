#include "Starfield/MapObservationInbox.h"

#include <utility>

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
    m_mapData.reset();
    m_markers.reset();
    m_dossier.reset();
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
        m_mapData.reset();
        m_markers.reset();
        m_dossier.reset();
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

    m_mapData.reset();
    m_markers.reset();
    m_dossier.reset();
    m_lifecycle[m_lifecycleCount++] = {
        .opening = opening,
        .identity = identity,
    };
}

void MapObservationInbox::RecordMapData(const MapSessionIdentity& identity, MapView view, FormID currentBodyId)
{
    std::lock_guard lock {m_mutex};

    if (!identity.IsValid() || identity != m_currentIdentity) {
        return;
    }

    if (!m_mapData) {
        m_mapData = MapDataObservation {
            .identity = identity,
            .view = view,
            .currentBodyId = currentBodyId,
        };
        return;
    }

    if (m_mapData->view != view) {
        m_mapData->view = MapView::Unknown;
    }
    if (m_mapData->currentBodyId != currentBodyId) {
        m_mapData->currentBodyId = 0;
    }
}

void MapObservationInbox::RecordMarkers(const MapSessionIdentity& identity, MarkerUpdate update)
{
    std::lock_guard lock {m_mutex};

    if (!identity.IsValid() || identity != m_currentIdentity) {
        return;
    }

    m_markers = MarkersObservation {
        .identity = identity,
        .update = std::move(update),
    };
}

void MapObservationInbox::RecordDossier(const MapSessionIdentity& identity, TargetObservation target)
{
    std::lock_guard lock {m_mutex};

    if (!identity.IsValid() || identity != m_currentIdentity) {
        return;
    }

    m_dossier = DossierObservation {
        .identity = identity,
        .target = std::move(target),
    };
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
        .mapData = m_mapData,
        .markers = m_markers,
        .dossier = m_dossier,
    };

    for (std::size_t index = 0; index < m_lifecycleCount; ++index) {
        observations.lifecycle[index] = m_lifecycle[index];
    }

    m_movieCreated = false;
    m_lifecycleCount = 0;
    m_lifecycleOverflowed = false;
    m_mapData.reset();
    m_markers.reset();
    m_dossier.reset();

    return observations;
}
