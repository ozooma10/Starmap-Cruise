#include "Application/CurrentSystemResolver.h"

#include <cstddef>
#include <unordered_map>

std::optional<FormID> ResolveCurrentSystem(
    std::span<const FormID> bodyIds,
    const BodyResolutionSource& bodySource)
{
    std::unordered_map<FormID, std::size_t> counts;

    for (const auto bodyId : bodyIds) {
        if (bodyId == 0)
            continue;

        const auto body = bodySource.ResolveBody(bodyId);
        if (body && body->id == bodyId)
            ++counts[body->systemId];
    }

    std::optional<FormID> bestSystem;
    std::size_t bestCount {0};
    bool tied {false};

    for (const auto& [systemId, count] : counts) {
        if (count > bestCount) {
            bestSystem = systemId;
            bestCount = count;
            tied = false;
        } else if (count == bestCount) {
            tied = true;
        }
    }

    if (tied)
        return std::nullopt;

    return bestSystem;
}
