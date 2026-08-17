#include "Map/MapSessionState.h"

#include <utility>

void MapSessionState::BeginMovie(std::uint32_t generation)
{
    movieGeneration_ = generation;
    ResetSession();
}

bool MapSessionState::Open(const MapOpenContext& context)
{
    if (!context.identity.IsValid() || context.identity.generation != movieGeneration_) {
        return false;
    }

    ResetSession();

    open_ = true;
    identity_ = context.identity;

    flyingAtOpen_ = context.flying;
    m_cruiseStateWhenOpened = context.cruiseState;
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

bool MapSessionState::SetDossier(const MapSessionIdentity& identity, TargetObservation dossier, std::optional<ResolvedBody> resolvedBody)
{
    if (!Accepts(identity)) {
        return false;
    }

    dossier_ = std::move(dossier);
    resolvedBody_ = std::move(resolvedBody);

    return true;
}

bool MapSessionState::CaptureCurrentSystem(const MapSessionIdentity& identity, FormID systemId)
{
    if (!Accepts(identity)) {
        return false;
    }

    if (!currentSystemId_) {
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
        .resolvedBody = resolvedBody_,
    };
}

ObservedCruiseState MapSessionState::CruiseStateWhenOpened() const
{
    return open_ ? m_cruiseStateWhenOpened : ObservedCruiseState::Unknown;
}

bool MapSessionState::IsActive(const MapSessionIdentity& identity) const
{
    return Accepts(identity);
}

bool MapSessionState::Accepts(const MapSessionIdentity& identity) const
{
    return open_ && identity.IsValid() && identity == identity_ && identity.generation == movieGeneration_;
}

void MapSessionState::ClearTargetObservations()
{
    highlightedMarkerCount_ = 0;
    marker_ = {};
    dossier_ = {};
    resolvedBody_.reset();
}

void MapSessionState::ResetSession()
{
    open_ = false;
    identity_ = {};

    flyingAtOpen_ = false;
    m_cruiseStateWhenOpened = ObservedCruiseState::Unknown;
    currentSystemId_.reset();

    view_ = MapView::Unknown;

    ClearTargetObservations();
}
