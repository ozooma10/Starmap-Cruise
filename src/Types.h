#pragma once

#include <cstdint>
#include <string>

namespace CFS
{
    enum class BodyKind : std::uint8_t
    {
        kPlanet = 2,
        kMoon = 3,
    };

    struct GalaxyIdentity
    {
        std::uint32_t system{ 0 };
        std::uint32_t parent{ 0 };
        std::uint32_t planet{ 0 };

        friend bool operator==(const GalaxyIdentity&, const GalaxyIdentity&) = default;
    };

    struct BodyDestination
    {
        BodyKind kind{ BodyKind::kPlanet };
        std::uint32_t formID{ 0 };
        GalaxyIdentity galaxy;
        std::string localizedName;
        std::uint32_t menuGeneration{ 0 };

        [[nodiscard]] explicit operator bool() const noexcept { return formID != 0; }
    };

    enum class NavState : std::uint8_t
    {
        kIdle,
        kMapSelection,
        kMarked,
        kAwaitingCruise,
        kAutopilotLocked,
    };
}

