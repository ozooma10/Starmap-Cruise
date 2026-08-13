#include "Bodies/PluginPlanetIndexer.h"

#include <utility>
#include <variant>

bool PluginPlanetIndexOutput::Succeeded() const
{
    return status == PluginPlanetIndexStatus::Success && std::holds_alternative<std::monostate>(failure);
}

PluginPlanetIndexOutput IndexPluginPlanets(std::span<const std::byte> pluginBytes, std::span<const ActivePluginIdentity> activePlugins, std::string_view pluginName)
{
    auto header = ParsePluginHeader(pluginBytes);

    if (!header.Succeeded()) {
        return {
            .status = PluginPlanetIndexStatus::HeaderParseFailed,
            .failure = std::move(header),
        };
    }

    auto resolver = BuildPluginFormIdResolver(activePlugins, pluginName, header.masters);

    if (!resolver.Succeeded()) {
        return {
            .status = PluginPlanetIndexStatus::ResolverBuildFailed,
            .failure = std::move(resolver),
        };
    }

    auto scan = ScanPluginPlanets(header.topLevelEntries, *resolver.resolver);

    if (!scan.Succeeded()) {
        // The scanner received the post-TES4 subspan, so normalize its entry
        // or record offset back to the beginning of the complete plugin.
        scan.failureOffset += header.topLevelOffset;

        return {
            .status = PluginPlanetIndexStatus::PlanetScanFailed,
            .failure = std::move(scan),
        };
    }

    return {
        .status = PluginPlanetIndexStatus::Success,
        .observations = std::move(scan.observations),
        .deletedBodyIds = std::move(scan.deletedBodyIds),
    };
}