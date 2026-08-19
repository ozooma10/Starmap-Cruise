#include "Map/MapSessionState.h"

#include <utility>

void MapSessionState::BeginMovie(std::uint32_t generation)
{
    m_movieGeneration = generation;
    ResetSession();
}

bool MapSessionState::Open(const MapOpenContext& context)
{
    if (!context.identity.IsValid() || context.identity.generation != m_movieGeneration) {
        return false;
    }

    ResetSession();

    m_open = true;
    m_identity = context.identity;

    m_flyingAtOpen = context.flying;
    m_cruiseStateWhenOpened = context.cruiseState;
    m_currentSystemId = context.currentSystemId;

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

    if (m_view != view) {
        m_view = view;
        ClearTargetObservations();
    }

    return true;
}

bool MapSessionState::SetMarkers(const MapSessionIdentity& identity, MarkerUpdate update)
{
    if (!Accepts(identity)) {
        return false;
    }

    m_highlightedMarkerCount = update.highlightedCount;

    if (update.highlightedCount == 1) {
        m_marker = std::move(update.highlighted);
    } else {
        m_marker = {};
    }

    return true;
}

bool MapSessionState::SetDossier(const MapSessionIdentity& identity, TargetObservation dossier, std::optional<ResolvedBody> resolvedBody)
{
    if (!Accepts(identity)) {
        return false;
    }

    m_dossier = std::move(dossier);
    m_resolvedBody = std::move(resolvedBody);

    return true;
}

bool MapSessionState::CaptureCurrentSystem(const MapSessionIdentity& identity, FormID systemId)
{
    if (!Accepts(identity)) {
        return false;
    }

    if (!m_currentSystemId) {
        m_currentSystemId = systemId;
        return true;
    }

    // Repeating the same resolution is harmless. A different value cannot rewrite the system captured by this map session.
    return m_currentSystemId == systemId;
}

bool MapSessionState::CaptureCurrentSystemForm(const MapSessionIdentity& identity, FormID systemFormId)
{
    if (!Accepts(identity) || systemFormId == 0) {
        return false;
    }

    if (!m_currentSystemFormId) {
        m_currentSystemFormId = systemFormId;
        return true;
    }

    return m_currentSystemFormId == systemFormId;
}

SelectionSnapshot MapSessionState::Snapshot() const
{
    const bool sessionValid = m_open && m_identity.IsValid() && m_identity.generation == m_movieGeneration;

    return {
        .sessionValid = sessionValid,
        .flying = m_flyingAtOpen,
        .systemView = m_view == MapView::System,
        .currentSystemId = m_currentSystemId,
        .currentSystemFormId = m_currentSystemFormId,
        .highlightedMarkerCount = m_highlightedMarkerCount,
        .marker = m_marker,
        .dossier = m_dossier,
        .resolvedBody = m_resolvedBody,
    };
}

ObservedCruiseState MapSessionState::CruiseStateWhenOpened() const
{
    return m_open ? m_cruiseStateWhenOpened : ObservedCruiseState::Unknown;
}

bool MapSessionState::IsActive(const MapSessionIdentity& identity) const
{
    return Accepts(identity);
}

bool MapSessionState::Accepts(const MapSessionIdentity& identity) const
{
    return m_open && identity.IsValid() && identity == m_identity && identity.generation == m_movieGeneration;
}

void MapSessionState::ClearTargetObservations()
{
    m_highlightedMarkerCount = 0;
    m_marker = {};
    m_dossier = {};
    m_resolvedBody.reset();
}

void MapSessionState::ResetSession()
{
    m_open = false;
    m_identity = {};

    m_flyingAtOpen = false;
    m_cruiseStateWhenOpened = ObservedCruiseState::Unknown;
    m_currentSystemId.reset();
    m_currentSystemFormId.reset();

    m_view = MapView::Unknown;

    ClearTargetObservations();
}
