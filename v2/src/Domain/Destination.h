#pragma once

#include <cstdint>
#include <optional>
#include <string>

using FormID = std::uint32_t;

enum class DestinationKind : std::uint8_t
{
    Planet,
    Moon,
    Station,
};

struct Destination
{
    DestinationKind kind {DestinationKind::Planet};

    // The identity presented publicly as the selected destination.
    FormID targetId {0};

    // The identity expected in the HUD course-lock feed.
    // This matches targetId for planets and moons, but may differ for stations.
    FormID courseId {0};

    std::optional<FormID> systemId;
    std::string displayName;

    bool IsValid() const
    {
        return targetId != 0 && courseId != 0 && systemId.has_value();
    }

    bool SameIdentityAs(const Destination& other) const
    {
        return kind == other.kind && targetId == other.targetId && courseId == other.courseId && systemId == other.systemId;
    }
};
