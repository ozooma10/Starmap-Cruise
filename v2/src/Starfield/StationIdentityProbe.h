#pragma once

#include "Map/MapSessionState.h"

#include "RE/Starfield.h"

#include <memory>

class StationIdentityProbe final
{
public:
    StationIdentityProbe();
    ~StationIdentityProbe();

    bool Enabled() const;
    void CaptureMapData(const MapSessionIdentity& identity, RE::Scaleform::GFx::Value& data);
    void CaptureMarkers(const MapSessionIdentity& identity, RE::Scaleform::GFx::Value& markers);
    void CaptureDossier(const MapSessionIdentity& identity, RE::Scaleform::GFx::Value& data);
    void CaptureHudCourse(std::uint32_t generation, FormID courseId, std::uint64_t publication);
    void OnHudMovieCreated(std::uint32_t generation);
    bool ObserveStockRouteInput(const RE::ButtonEvent& event, std::uint64_t latestHudPublication);
    void OnMapClosed(const MapSessionIdentity& identity);
    void CancelRouteAttempt();
    void Drain(const MapSessionIdentity& activeIdentity);
    void Invalidate();

private:
    struct State;
    std::unique_ptr<State> m_state;
};
