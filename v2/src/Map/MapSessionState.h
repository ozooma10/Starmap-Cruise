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

struct MapOpenContext
{
    MapSessionIdentity identity;

    bool flying {false};
    bool cruiseWasActive {false};

    FormID currentSystemId {0};
};

struct MarkerUpdate
{
    std::size_t highlightedCount {0};
    TargetObservation highlighted;
};

struct BodyResolutionUpdate
{
    // Identifies the dossier for which this resolution was produced.
    // A delayed resolution for an older dossier is rejected.
    FormID dossierId {0};

    bool dossierIsLiveBody {false};
    bool bodyIndexReady {false};

    std::optional<IndexedBodyObservation> indexedBody;
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

    bool SetDossier(const MapSessionIdentity& identity, TargetObservation dossier);

    bool SetBodyResolution(const MapSessionIdentity& identity, BodyResolutionUpdate resolution);

    // Allows the first late cockpit-system resolution to complete an already-open session. Once captured, it cannot be rewritten.
    bool CaptureCurrentSystem(const MapSessionIdentity& identity, FormID systemId);

    SelectionSnapshot Snapshot() const;

    bool CruiseWasActiveWhenOpened() const;
    bool IsActive(const MapSessionIdentity& identity) const;

private:
    bool Accepts(const MapSessionIdentity& identity) const;

    void ClearBodyResolution();
    void ClearTargetObservations();
    void ResetSession();

    std::uint32_t movieGeneration_ {0};

    bool open_ {false};
    MapSessionIdentity identity_;

    bool flyingAtOpen_ {false};
    bool cruiseWasActiveAtOpen_ {false};
    FormID currentSystemId_ {0};

    MapView view_ {MapView::Unknown};

    std::size_t highlightedMarkerCount_ {0};
    TargetObservation marker_;
    TargetObservation dossier_;

    bool dossierIsLiveBody_ {false};
    bool bodyIndexReady_ {false};
    std::optional<IndexedBodyObservation> indexedBody_;
};
