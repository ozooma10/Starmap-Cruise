#pragma once

#include "Bodies/PluginHeaderParser.h"
#include "Bodies/PluginPlanetScanner.h"
#include "Bodies/PluginResolverBuilder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

enum class PluginPlanetIndexStatus : std::uint8_t
{
    Success,

    HeaderParseFailed,
    ResolverBuildFailed,
    PlanetScanFailed,
};

// Exactly one existing subsystem output describes a failed stage. Successful results retain monostate.
using PluginPlanetIndexFailure = std::variant<std::monostate, PluginHeaderParseOutput, PluginResolverBuildOutput, PluginPlanetScanOutput>;

struct PluginPlanetIndexOutput
{
    PluginPlanetIndexStatus status {PluginPlanetIndexStatus::HeaderParseFailed};

    std::vector<IndexedBodyObservation> observations;
    std::vector<FormID> deletedBodyIds;

    PluginPlanetIndexFailure failure;

    bool Succeeded() const;
};

// Indexes one complete, already-loaded plugin. pluginName must identify these bytes within activePlugins. 
// Scan failure offsets are translated to be relative to the beginning of pluginBytes.
PluginPlanetIndexOutput IndexPluginPlanets(std::span<const std::byte> pluginBytes, std::span<const ActivePluginIdentity> activePlugins, std::string_view pluginName);