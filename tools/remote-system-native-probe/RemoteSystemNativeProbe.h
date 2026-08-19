#pragma once

#ifndef CFS_REMOTE_NATIVE_PROBE
#error "RemoteSystemNativeProbe is diagnostic-only and requires CFS_REMOTE_NATIVE_PROBE"
#endif

#include "Map/MapSessionState.h"

#include "RE/Starfield.h"

#include <cstdint>

namespace CFS::RemoteSystemNativeProbe
{
    // This facade is deliberately passive. Every Record* entry point copies
    // only bounded values; Drain owns all live-engine reads and all logging on
    // the verified post-UI-advance lane.
    [[nodiscard]] bool Initialize() noexcept;

    void RecordMapMovie(const MapSessionIdentity& identity,
        std::int64_t bornTicks) noexcept;
    void RecordMenuLifecycle(const char* menuName, bool opening) noexcept;
    void RecordMapLifecycle(const MapSessionIdentity& identity,
        bool opening) noexcept;
    void RecordMapData(const MapSessionIdentity& identity, MapView view,
        FormID currentBodyId, FormID currentSystemFormId) noexcept;
    void RecordMarkers(const MapSessionIdentity& identity,
        const MarkerUpdate& update) noexcept;
    void RecordDossier(const MapSessionIdentity& identity,
        const TargetObservation& target) noexcept;
    void RecordResolvedMarker(const MapSessionIdentity& identity,
        const TargetObservation& target) noexcept;
    void RecordHudCourse(std::uint32_t generation,
        RE::Scaleform::GFx::Value& payload) noexcept;
    void RecordHudMovieCreated(std::uint32_t generation) noexcept;
    void RecordInput(const RE::ButtonEvent& event) noexcept;

    void Drain(const MapSessionIdentity& activeMapIdentity,
        std::uint32_t hudGeneration) noexcept;
}
