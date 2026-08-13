#include "Map/MapSessionState.h"

#include <utility>
#include "MapSessionState.h"

void MapSessionState::BeginMovie(std::uint32_t generation)
{
    movieGeneration_ = generation;
    ResetSession();
}

bool MapSessionState::Open(const MapOpenContext& context)
{
    if (!context.identity.IsValid() ||
        context.identity.generation != movieGeneration_) {
        return false;
    }

    ResetSession();

    open_ = true;
    identity_ = context.identity;

    flyingAtOpen_ = context.flying;
    cruiseWasActiveAtOpen_ = context.cruiseWasActive;
    currentSystemId_ = context.currentSystemId;

    return true;
}

bool MapSessionState::Close(const MapSessionIdentity& identity)
{
    if (!Accepts(identity)) {
        return false;
    }

    ResetSession();
    return true;
}

bool MapSessionState::SetView(const MapSessionIdentity& identity, MapView view)
{
    if (!Accepts(identity)) {
        return false;
    }

    if (view_ != view) {
        view_ = view;
        ClearTargetObservations();
    }

    return true;
}

bool MapSessionState::SetMarkers(const MapSessionIdentity& identity, MarkerUpdate update)
{
    if (!Accepts(identity)) {
        return false;
    }

    highlightedMarkerCount_ = update.highlightedCount;

    if (update.highlightedCount == 1) {
        marker_ = std::move(update.highlighted);
    } else {
        marker_ = {};
    }

    return true;
}

bool MapSessionState::SetDossier(
    const MapSessionIdentity& identity,
    TargetObservation dossier)
{
    if (!Accepts(identity)) {
        return false;
    }

    const bool identityChanged = dossier_.id != dossier.id || dossier_.kind != dossier.kind;

    dossier_ = std::move(dossier);

    if (identityChanged) {
        ClearBodyResolution();
    }

    return true;
}

bool MapSessionState::SetBodyResolution(const MapSessionIdentity& identity, BodyResolutionUpdate resolution)
{
    if (!Accepts(identity)) {
        return false;
    }

    if (dossier_.id == 0 || resolution.dossierId != dossier_.id) {
        return false;
    }

    dossierIsLiveBody_ = resolution.dossierIsLiveBody;
    bodyIndexReady_ = resolution.bodyIndexReady;

    if (resolution.bodyIndexReady) {
        indexedBody_ = std::move(resolution.indexedBody);
    } else {
        indexedBody_.reset();
    }

    return true;
}

bool MapSessionState::CaptureCurrentSystem(const MapSessionIdentity& identity, FormID systemId)
{
    if (!Accepts(identity) || systemId == 0) {
        return false;
    }

    if (currentSystemId_ == 0) {
        currentSystemId_ = systemId;
        return true;
    }

    // Repeating the same resolution is harmless. A different value cannot rewrite the system captured by this map session.
    return currentSystemId_ == systemId;
}

SelectionSnapshot MapSessionState::Snapshot() const
{
    const bool sessionValid = open_ && identity_.IsValid() && identity_.generation == movieGeneration_;

    return {
        .sessionValid = sessionValid,
        .flying = flyingAtOpen_,
        .systemView = view_ == MapView::System,
        .currentSystemId = currentSystemId_,
        .highlightedMarkerCount = highlightedMarkerCount_,
        .marker = marker_,
        .dossier = dossier_,
        .dossierIsLiveBody = dossierIsLiveBody_,
        .bodyIndexReady = bodyIndexReady_,
        .indexedBody = indexedBody_,
    };
}

bool MapSessionState::CruiseWasActiveWhenOpened() const
{
    return open_ && cruiseWasActiveAtOpen_;
}

bool MapSessionState::IsActive(const MapSessionIdentity &identity) const
{
    return Accepts(identity);
}
bool MapSessionState::Accepts(const MapSessionIdentity& identity) const
{
    return open_ && identity.IsValid() && identity == identity_ && identity.generation == movieGeneration_;
}

void MapSessionState::ClearBodyResolution()
{
    dossierIsLiveBody_ = false;
    bodyIndexReady_ = false;
    indexedBody_.reset();
}

void MapSessionState::ClearTargetObservations()
{
    highlightedMarkerCount_ = 0;
    marker_ = {};
    dossier_ = {};

    ClearBodyResolution();
}

void MapSessionState::ResetSession()
{
    open_ = false;
    identity_ = {};

    flyingAtOpen_ = false;
    cruiseWasActiveAtOpen_ = false;
    currentSystemId_ = 0;

    view_ = MapView::Unknown;

    ClearTargetObservations();
}