#pragma once

#include "Bodies/PluginFormId.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ActivePluginIdentity
{
    std::string name;
    PluginLoadIdentity identity;
};

enum class PluginResolverBuildStatus : std::uint8_t
{
    Success,

    InvalidActivePluginName,
    InvalidActivePluginIdentity,
    DuplicateActivePluginName,
    DuplicateActivePluginIdentity,

    InvalidSelfPluginName,
    SelfPluginNotActive,

    TooManyMasters,
    InvalidMasterName,
    DuplicateMasterName,
    SelfListedAsMaster,
    MasterPluginNotActive,
};

struct PluginResolverBuildOutput
{
    PluginResolverBuildStatus status {PluginResolverBuildStatus::InvalidActivePluginName};

    std::optional<PluginFormIdResolver> resolver;

    // Identifies the offending input when the corresponding status applies.
    std::optional<std::size_t> activePluginIndex;
    std::optional<std::size_t> masterIndex;

    bool Succeeded() const;
};

PluginResolverBuildOutput BuildPluginFormIdResolver(std::span<const ActivePluginIdentity> activePlugins, std::string_view selfName, std::span<const std::string> masterNames);