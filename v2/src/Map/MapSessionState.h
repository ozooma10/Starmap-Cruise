#pragma once

#include "Selection/SelectionPolicy.h"

#include <cstddef>
#include <cstdint>
#include <optional>

struct MapSessionIdentity
{
    std::uint32_t session {0};
    std::uint32_t generation {0};

    bool IsValid() const
    {
        return session != 0 && generation != 0;
    }

    friend bool operator==(const MapSessionIdentity&, const MapSessionIdentity&) = default;
};

enum class MapView : std::uint8_t
{
    Unknown,
    Galaxy,
    System,
    Other,
};

enum class ObservedCruiseState : std::uint8_t
{
    Unknown,
    Inactive,
    Active,
};

struct MapOpenContext
{
    MapSessionIdentity identity;

    bool flying {false};
    ObservedCruiseState cruiseState {ObservedCruiseState::Unknown};

    std::optional<FormID> currentSystemId;
};

struct MarkerUpdate
{
    std::size_t highlightedCount {0};
    TargetObservation highlighted;
};

class MapSessionState
{
public:
    // Invalidates any session belonging to the previous movie.
    void BeginMovie(std::uint32_t generation);

    bool Open(const MapOpenContext& context);
    bool Close(const MapSessionIdentity& identity);

    bool SetView(const MapSessionIdentity& identity, MapView view);

    bool SetMarkers(const MapSessionIdentity& identity, MarkerUpdate update);

    bool SetDossier(const MapSessionIdentity& identity, TargetObservation dossier, std::optional<ResolvedBody> resolvedBody);

    // Allows the first late cockpit-system resolution to complete an already-open session. Once captured, it cannot be rewritten.
    bool CaptureCurrentSystem(const MapSessionIdentity& identity, FormID systemId);
    // Captures the map-independent player STDT separately from the numeric galaxy-system ID.
    bool CaptureCurrentSystemForm(const MapSessionIdentity& identity, FormID systemFormId);

    SelectionSnapshot Snapshot() const;

    ObservedCruiseState CruiseStateWhenOpened() const;
    bool IsActive(const MapSessionIdentity& identity) const;

private:
    bool Accepts(const MapSessionIdentity& identity) const;

    void ClearTargetObservations();
    void ResetSession();

    std::uint32_t m_movieGeneration {0};

    bool m_open {false};
    MapSessionIdentity m_identity;

    bool m_flyingAtOpen {false};
    ObservedCruiseState m_cruiseStateWhenOpened {ObservedCruiseState::Unknown};
    std::optional<FormID> m_currentSystemId;
    std::optional<FormID> m_currentSystemFormId;

    MapView m_view {MapView::Unknown};

    std::size_t m_highlightedMarkerCount {0};
    TargetObservation m_marker;
    TargetObservation m_dossier;

    std::optional<ResolvedBody> m_resolvedBody;
};
