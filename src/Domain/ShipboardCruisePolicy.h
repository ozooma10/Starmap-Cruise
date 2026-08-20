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

[[nodiscard]] constexpr CruiseCommandPath SelectCruiseCommandPath(const ShipContext& opened, bool hudAvailable, bool nativeAvailable) noexcept
{
    if (opened.playerPiloting) {
        return hudAvailable ? CruiseCommandPath::Hud : CruiseCommandPath::Unavailable;
    }
    return nativeAvailable && opened.IsShipboard() ? CruiseCommandPath::Native : CruiseCommandPath::Unavailable;
}

[[nodiscard]] constexpr ShipboardActivationMode DecideShipboardActivation(
    const ShipContext& opened,
    const ShipContext& live,
    bool cruiseActive,
    bool vanillaEligible,
    bool nativeAvailable) noexcept
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
