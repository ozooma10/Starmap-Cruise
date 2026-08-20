#pragma once

#include "Domain/ShipContext.h"
#include "Domain/ShipboardCruisePolicy.h"
#include "Map/MapSessionState.h"

#include <cstdint>

enum class CruiseControlSource : std::uint8_t
{
    Unavailable,
    Hud,
    Native,
};

struct CruiseControlSnapshot
{
    ObservedCruiseState cruiseState {ObservedCruiseState::Unknown};
    bool engageAvailable {false};
    FormID currentCourseId {0};
    CruiseControlSource source {CruiseControlSource::Unavailable};
};

class ShipboardCruiseBridge final
{
public:
    bool Initialize();

    [[nodiscard]] bool Available() const noexcept;
    [[nodiscard]] CruiseControlSnapshot Read(const ShipContext& opened, const ShipContext& live) const;
    [[nodiscard]] ObservedCruiseState ReadState() const;
    [[nodiscard]] FormID ReadCurrentCourse() const;

    bool Start(const ShipContext& opened, const ShipContext& live, bool& usedGuardedFallback) const;
    bool SetCourse(FormID courseId) const;
    bool RefreshCourse(FormID courseId) const;

private:
    std::uintptr_t m_currentCourseAddress {0};
    bool m_available {false};
};
