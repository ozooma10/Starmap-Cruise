#pragma once

#include "Domain/ShipContext.h"

#include <cstdint>

enum class CruiseCommandPath : std::uint8_t
{
    Unavailable,
    Hud,
    Native,
};

enum class ShipboardActivationMode : std::uint8_t
{
    Rejected,
    VanillaEligible,
    GuardedFreeRoam,
};

enum class CourseDispatchPath : std::uint8_t
{
    Unavailable,
    AlreadyLocked,
    Hud,
    HudRefresh,
    Native,
    NativeRefresh,
};

[[nodiscard]] constexpr ShipContext SelectCruiseCommandContext(const ShipContext& opened, const ShipContext& live, bool postTravel) noexcept
{
    return postTravel ? live : opened;
}

[[nodiscard]] constexpr CruiseCommandPath SelectCruiseCommandPath(const ShipContext& opened, bool hudAvailable, bool nativeAvailable) noexcept
{
    if (opened.playerPiloting) {
        return hudAvailable ? CruiseCommandPath::Hud : CruiseCommandPath::Unavailable;
    }
    return nativeAvailable && opened.IsShipboard() ? CruiseCommandPath::Native : CruiseCommandPath::Unavailable;
}

[[nodiscard]] constexpr CourseDispatchPath SelectCourseDispatchPath(const ShipContext& opened, bool hudCruiseActive, bool nativeCruiseActive, bool followsCruiseActivation, FormID requestedCourseId, FormID hudLockedCourseId, FormID nativeCourseId) noexcept
{
    if (requestedCourseId == 0 || !opened.IsShipboard()) {
        return CourseDispatchPath::Unavailable;
    }

    if (opened.playerPiloting) {
        if (!hudCruiseActive) {
            return CourseDispatchPath::Unavailable;
        }
        if (!followsCruiseActivation && hudLockedCourseId == requestedCourseId) {
            return CourseDispatchPath::AlreadyLocked;
        }
        if (nativeCourseId == requestedCourseId) {
            return CourseDispatchPath::HudRefresh;
        }
        return CourseDispatchPath::Hud;
    }

    if (!nativeCruiseActive) {
        return CourseDispatchPath::Unavailable;
    }
    if (nativeCourseId == requestedCourseId) {
        return followsCruiseActivation ? CourseDispatchPath::NativeRefresh : CourseDispatchPath::AlreadyLocked;
    }
    return CourseDispatchPath::Native;
}

[[nodiscard]] constexpr ShipboardActivationMode DecideShipboardActivation(const ShipContext& opened, const ShipContext& live, bool cruiseActive, bool vanillaEligible, bool nativeAvailable) noexcept
{
    if (!nativeAvailable || cruiseActive || !opened.CanStartCruise() || !live.CanStartCruise() || !opened.SameShipAs(live)) {
        return ShipboardActivationMode::Rejected;
    }
    if (vanillaEligible) {
        return ShipboardActivationMode::VanillaEligible;
    }
    if (!opened.playerPiloting && !live.playerPiloting) {
        return ShipboardActivationMode::GuardedFreeRoam;
    }
    return ShipboardActivationMode::Rejected;
}
