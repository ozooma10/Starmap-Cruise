#include "Bodies/PluginResolverBuilder.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
    constexpr std::size_t MaximumMasterCount = 0xFF;

    std::string FoldAscii(std::string_view text)
    {
        std::string folded {text};

        for (auto& character : folded) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character + ('a' - 'A'));
            }
        }

        return folded;
    }

    bool IsValidPluginName(std::string_view name)
    {
        if (name.empty() || name.find('\0') != std::string_view::npos || name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos) {
            return false;
        }

        const auto folded = FoldAscii(name);

        return folded.ends_with(".esm") || folded.ends_with(".esp") || folded.ends_with(".esl");
    }

    std::uint32_t IdentityKey(const PluginLoadIdentity& identity)
    {
        return (static_cast<std::uint32_t>(identity.tier) << 16) | identity.index;
    }

    PluginResolverBuildOutput Failure(PluginResolverBuildStatus status)
    {
        return {
            .status = status,
        };
    }

    PluginResolverBuildOutput ActivePluginFailure(PluginResolverBuildStatus status, std::size_t index)
    {
        return {
            .status = status,
            .activePluginIndex = index,
        };
    }

    PluginResolverBuildOutput MasterFailure(PluginResolverBuildStatus status, std::size_t index)
    {
        return {
            .status = status,
            .masterIndex = index,
        };
    }
}

bool PluginResolverBuildOutput::Succeeded() const
{
    return status == PluginResolverBuildStatus::Success && resolver.has_value() && resolver->IsValid();
}

PluginResolverBuildOutput BuildPluginFormIdResolver(std::span<const ActivePluginIdentity> activePlugins, std::string_view selfName, std::span<const std::string> masterNames)
{
    std::unordered_map<std::string, PluginLoadIdentity> identitiesByName;
    std::unordered_set<std::uint32_t> identityKeys;

    identitiesByName.reserve(activePlugins.size());
    identityKeys.reserve(activePlugins.size());

    for (std::size_t index = 0; index < activePlugins.size(); ++index) {
        const auto& plugin = activePlugins[index];

        if (!IsValidPluginName(plugin.name)) {
            return ActivePluginFailure(PluginResolverBuildStatus::InvalidActivePluginName, index);
        }

        if (!plugin.identity.IsValid()) {
            return ActivePluginFailure(PluginResolverBuildStatus::InvalidActivePluginIdentity, index);
        }

        const auto foldedName = FoldAscii(plugin.name);

        if (identitiesByName.contains(foldedName)) {
            return ActivePluginFailure(PluginResolverBuildStatus::DuplicateActivePluginName, index);
        }

        if (!identityKeys.emplace(IdentityKey(plugin.identity)).second) {
            return ActivePluginFailure(PluginResolverBuildStatus::DuplicateActivePluginIdentity, index);
        }

        identitiesByName.emplace(foldedName, plugin.identity);
    }

    if (!IsValidPluginName(selfName)) {
        return Failure(PluginResolverBuildStatus::InvalidSelfPluginName);
    }

    const auto foldedSelfName = FoldAscii(selfName);
    const auto self = identitiesByName.find(foldedSelfName);

    if (self == identitiesByName.end()) {
        return Failure(PluginResolverBuildStatus::SelfPluginNotActive);
    }

    if (masterNames.size() > MaximumMasterCount) {
        return Failure(PluginResolverBuildStatus::TooManyMasters);
    }

    std::unordered_set<std::string> seenMasterNames;
    std::vector<PluginLoadIdentity> masters;

    seenMasterNames.reserve(masterNames.size());
    masters.reserve(masterNames.size());

    for (std::size_t index = 0; index < masterNames.size(); ++index) {
        const auto& masterName = masterNames[index];

        if (!IsValidPluginName(masterName)) {
            return MasterFailure(PluginResolverBuildStatus::InvalidMasterName, index);
        }

        const auto foldedMasterName = FoldAscii(masterName);

        if (!seenMasterNames.emplace(foldedMasterName).second) {
            return MasterFailure(PluginResolverBuildStatus::DuplicateMasterName, index);
        }

        if (foldedMasterName == foldedSelfName) {
            return MasterFailure(PluginResolverBuildStatus::SelfListedAsMaster, index);
        }

        const auto master = identitiesByName.find(foldedMasterName);

        if (master == identitiesByName.end()) {
            return MasterFailure(PluginResolverBuildStatus::MasterPluginNotActive, index);
        }

        masters.push_back(master->second);
    }

    return {
        .status = PluginResolverBuildStatus::Success,
        .resolver = PluginFormIdResolver {
            std::move(masters),
            self->second,
        },
    };
}