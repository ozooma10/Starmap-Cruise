#pragma once

#include "Domain/Destination.h"

struct ShipContext
{
    FormID shipId {0};
    bool aboardPlayerShip {false};
    bool inSpace {false};
    bool playerPiloting {false};
    bool landed {false};
    bool docked {false};
    bool loading {false};
    bool jumpInProgress {false};
    bool playerActorInCombat {false};
    bool flightSettled {false};

    [[nodiscard]] bool IsShipboard() const noexcept
    {
        return shipId != 0 && aboardPlayerShip && inSpace && !landed && !docked;
    }

    [[nodiscard]] bool CanStartCruise() const noexcept
    {
        return IsShipboard() && !loading && !jumpInProgress && flightSettled;
    }

    [[nodiscard]] bool SameShipAs(const ShipContext& other) const noexcept
    {
        return shipId != 0 && shipId == other.shipId;
    }

    friend bool operator==(const ShipContext&, const ShipContext&) = default;
};
