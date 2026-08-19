#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using FormID = std::uint32_t;

enum class DestinationKind : std::uint8_t
{
    Planet,
    Moon,
    Station,
};

struct SystemIdentity
{
    // Runtime STDT FormID. A valid identity always has a nonzero star form.
    FormID starFormId {0};

    // Runtime numeric galaxy-system ID. Zero is the valid identity for Sol.
    FormID numericId {0};

    bool IsValid() const
    {
        return starFormId != 0;
    }

    friend bool operator==(const SystemIdentity&, const SystemIdentity&) = default;
};

struct RemoteTargetPlan
{
    // Nearest-parent-first PNDT FormIDs accepted while Starfield resolves a
    // latent final moon target. Direct planets legitimately have no entries.
    std::vector<FormID> allowedWaypointIds;

    bool IsValid() const
    {
        for (std::size_t index = 0; index < allowedWaypointIds.size(); ++index) {
            if (allowedWaypointIds[index] == 0 ||
                std::find(allowedWaypointIds.begin(), allowedWaypointIds.begin() + index, allowedWaypointIds[index]) != allowedWaypointIds.begin() + index) {
                return false;
            }
        }
        return true;
    }

    friend bool operator==(const RemoteTargetPlan&, const RemoteTargetPlan&) = default;
};

struct Destination
{
    DestinationKind kind {DestinationKind::Planet};

    // The identity presented publicly as the selected destination.
    FormID targetId {0};

    // The identity expected in the HUD course-lock feed.
    // This matches targetId for planets and moons, but may differ for stations.
    FormID courseId {0};

    SystemIdentity system;
    RemoteTargetPlan remotePlan;
    std::string displayName;

    bool IsValid() const
    {
        return targetId != 0 && courseId != 0 && system.IsValid() && remotePlan.IsValid();
    }

    bool SameIdentityAs(const Destination& other) const
    {
        return kind == other.kind && targetId == other.targetId && courseId == other.courseId && system == other.system && remotePlan == other.remotePlan;
    }
};
