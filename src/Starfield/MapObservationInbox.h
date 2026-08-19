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

    enum class Action : std::uint8_t
    {
        Tap,
        HoldCompleted,
    };

    struct LifecycleObservation
    {
        bool opening {false};
        MapSessionIdentity identity;
    };

    struct MapDataObservation
    {
        MapSessionIdentity identity;
        MapView view {MapView::Unknown};
        FormID currentSystemFormId {0};
    };

    struct MarkersObservation
    {
        MapSessionIdentity identity;
        MarkerUpdate update;
    };

    struct DossierObservation
    {
        MapSessionIdentity identity;
        TargetObservation target;
    };

    struct ActionObservation
    {
        MapSessionIdentity identity;
        Action action {Action::Tap};
    };

    struct Observations
    {
        bool movieCreated {false};
        std::uint32_t movieGeneration {0};
        std::int64_t movieBornTicks {0};

        std::array<LifecycleObservation, MaxLifecycleObservations> lifecycle;
        std::size_t lifecycleCount {0};
        bool lifecycleOverflowed {false};

        std::optional<MapDataObservation> mapData;
        std::optional<MarkersObservation> markers;
        std::optional<DossierObservation> dossier;
        std::optional<ActionObservation> action;
        bool actionOverflowed {false};
    };

    void RecordMovieCreated(std::int64_t bornTicks);
    void RecordLifecycle(bool opening);
    void RecordMapData(const MapSessionIdentity& identity, MapView view, FormID currentSystemFormId);
    void RecordMarkers(const MapSessionIdentity& identity, MarkerUpdate update);
    void RecordDossier(const MapSessionIdentity& identity, TargetObservation target);
    void RecordAction(const MapSessionIdentity& identity, Action action);
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

    std::optional<MapDataObservation> m_mapData;
    std::optional<MarkersObservation> m_markers;
    std::optional<DossierObservation> m_dossier;
    std::optional<ActionObservation> m_action;
    bool m_actionOverflowed {false};
};
