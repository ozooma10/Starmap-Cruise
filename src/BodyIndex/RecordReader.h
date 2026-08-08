#pragma once

#include "BodyIndex.h"

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CFS::BodyIndex::RecordReader
{
    enum class PluginTier : std::uint8_t
    {
        kFull,
        kSmall,
        kMedium,
    };

    struct PluginInfo
    {
        std::string name;
        PluginTier tier{ PluginTier::kFull };
        std::uint16_t index{ 0 };
    };

    enum class LiveFormKind : std::uint8_t
    {
        kPlanetData,
        kStarData,
    };

    using LiveFormPredicate =
        std::function<bool(std::uint32_t, LiveFormKind)>;

    struct Diagnostic
    {
        bool warning{ false };
        std::string message;
    };

    struct Result
    {
        std::unordered_map<std::uint32_t, Entry> entries;
        std::unordered_map<std::uint32_t, std::uint32_t> systemRoots;
        std::unordered_set<std::uint32_t> stationBases;
        std::unordered_map<std::uint32_t, std::vector<StationTarget>> stationTargets;
        std::unordered_map<std::uint32_t, std::vector<IndexedBody>> stationOrbitals;
        std::size_t stationReferenceCount{ 0 };
        std::size_t stationCourseMarkerCount{ 0 };
        std::size_t stationOrbitalCount{ 0 };
        std::vector<Diagnostic> diagnostics;
    };

    [[nodiscard]] Result ReadLoadOrder(const std::filesystem::path& a_dataRoot,
        std::span<const PluginInfo> a_plugins,
        const LiveFormPredicate& a_isLiveForm);
}
