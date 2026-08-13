#pragma once

#include "Domain/Destination.h"

#include <cstdint>
#include <optional>
#include <vector>

enum class PluginTier : std::uint8_t
{
    Full,
    Medium,
    Small,
};

struct PluginLoadIdentity
{
    PluginTier tier {PluginTier::Full};
    std::uint16_t index {0};

    bool IsValid() const;
};

class PluginFormIdResolver
{
public:
    PluginFormIdResolver(std::vector<PluginLoadIdentity> masters, PluginLoadIdentity self);

    bool IsValid() const;

    std::optional<FormID> Resolve(FormID pluginFormId) const;

private:
    std::vector<PluginLoadIdentity> masters_;
    PluginLoadIdentity self_;
};