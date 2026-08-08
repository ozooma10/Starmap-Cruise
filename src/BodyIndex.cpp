#include "BodyIndex.h"

#include "BodyIndex/RecordReader.h"

#include "RE/Starfield.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CFS::BodyIndex
{
    namespace
    {
        using RecordReader::PluginInfo;
        using RecordReader::PluginTier;

        std::mutex g_mutex;
        std::unordered_map<std::uint32_t, Entry> g_entries;
        std::unordered_map<std::uint32_t, std::uint32_t> g_systemRoots;
        std::unordered_set<std::uint32_t> g_stationBases;
        std::unordered_map<std::uint32_t, std::vector<StationTarget>> g_stationTargets;
        std::unordered_map<std::uint32_t, std::vector<IndexedBody>> g_stationOrbitals;
        std::atomic<bool> g_started{ false };
        std::atomic<bool> g_ready{ false };

        std::vector<PluginInfo> CollectPlugins()
        {
            std::vector<PluginInfo> plugins;
            const auto handler = RE::TESDataHandler::GetSingleton();
            if (!handler)
                return plugins;

            const auto append = [&plugins](const auto& a_files, PluginTier a_tier) {
                std::uint16_t tierIndex = 0;
                for (const auto* file : a_files) {
                    if (!file) {
                        ++tierIndex;
                        continue;
                    }
                    const char* raw = file->fileName;
                    const auto length = ::strnlen(raw, sizeof(file->fileName));
                    if (length == 0 || length >= sizeof(file->fileName))
                        return false;
                    std::string name{ raw, length };
                    if (!(name.ends_with(".esm") || name.ends_with(".esp") ||
                        name.ends_with(".esl")))
                        return false;
                    const auto index = a_tier == PluginTier::kFull ?
                        static_cast<std::uint16_t>(file->compileIndex) : tierIndex;
                    plugins.push_back({ std::move(name), a_tier, index });
                    ++tierIndex;
                }
                return true;
            };

            if (!append(handler->compiledFileCollection.files, PluginTier::kFull) ||
                !append(handler->compiledFileCollection.smallFiles, PluginTier::kSmall) ||
                !append(handler->compiledFileCollection.mediumFiles, PluginTier::kMedium)) {
                REX::WARN("[bodies] active plugin collection failed validation");
                return {};
            }

            REX::INFO("[bodies] active plugin tiers: full={} small={} medium={} total={}",
                handler->compiledFileCollection.files.size(),
                handler->compiledFileCollection.smallFiles.size(),
                handler->compiledFileCollection.mediumFiles.size(), plugins.size());
            return plugins;
        }
    }

    void StartLoad()
    {
        if (g_started.exchange(true, std::memory_order_acq_rel))
            return;
        const auto plugins = CollectPlugins();
        std::thread{ [plugins] {
            const auto started = std::chrono::steady_clock::now();
            auto index = RecordReader::ReadLoadOrder("Data", plugins,
                [](std::uint32_t a_formID, RecordReader::LiveFormKind a_kind) {
                    const auto form = RE::TESForm::LookupByID(a_formID);
                    if (!form)
                        return false;
                    const auto expected = a_kind == RecordReader::LiveFormKind::kPlanetData ?
                        RE::FormType::kPNDT : RE::FormType::kSTDT;
                    return form->GetFormType() == expected;
                });

            for (const auto& diagnostic : index.diagnostics) {
                if (diagnostic.warning)
                    REX::WARN("[bodies] {}", diagnostic.message);
                else
                    REX::INFO("[bodies] {}", diagnostic.message);
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            const auto count = index.entries.size();
            const auto systemRootCount = index.systemRoots.size();
            const auto stationBaseCount = index.stationBases.size();
            const auto stationCellCount = index.stationTargets.size();
            const auto stationReferenceCount = index.stationReferenceCount;
            const auto stationCourseMarkerCount = index.stationCourseMarkerCount;
            const auto stationOrbitalCount = index.stationOrbitalCount;
            {
                std::lock_guard lock{ g_mutex };
                g_entries = std::move(index.entries);
                g_systemRoots = std::move(index.systemRoots);
                g_stationBases = std::move(index.stationBases);
                g_stationTargets = std::move(index.stationTargets);
                g_stationOrbitals = std::move(index.stationOrbitals);
            }
            g_ready.store(true, std::memory_order_release);
            REX::INFO("[bodies] load-order index ready: {} PNDT bodies, {} STDT system roots, "
                      "{} station bases, {} placed station refs in {} cells, "
                      "{} exact station CELL/XMRK course links, "
                      "{} CELL/PNDT orbital links, {} ms",
                count, systemRootCount, stationBaseCount, stationReferenceCount,
                stationCellCount, stationCourseMarkerCount, stationOrbitalCount,
                elapsed);
        } }.detach();
    }

    bool Ready() { return g_ready.load(std::memory_order_acquire); }

    std::optional<Entry> Lookup(std::uint32_t a_formID)
    {
        std::lock_guard lock{ g_mutex };
        if (const auto found = g_entries.find(a_formID); found != g_entries.end())
            return found->second;
        return std::nullopt;
    }

    std::vector<IndexedBody> ParentPlanets(std::uint32_t a_moonFormID)
    {
        std::vector<IndexedBody> parents;
        std::lock_guard lock{ g_mutex };
        const auto moon = g_entries.find(a_moonFormID);
        if (moon == g_entries.end() || !moon->second.galaxy.parent)
            return parents;

        for (const auto& [formID, entry] : g_entries) {
            if (entry.galaxy.system == moon->second.galaxy.system &&
                entry.galaxy.parent == 0 &&
                entry.galaxy.planet == moon->second.galaxy.parent) {
                parents.push_back({ formID, entry.galaxy, entry.editorID });
            }
        }
        std::ranges::sort(parents, {}, &IndexedBody::formID);
        return parents;
    }

    std::vector<IndexedBody> ParentBodies(std::uint32_t a_childFormID)
    {
        std::vector<IndexedBody> parents;
        std::lock_guard lock{ g_mutex };
        const auto child = g_entries.find(a_childFormID);
        if (child == g_entries.end() || !child->second.galaxy.parent)
            return parents;

        for (const auto& [formID, entry] : g_entries) {
            if (formID != a_childFormID &&
                entry.galaxy.system == child->second.galaxy.system &&
                entry.galaxy.planet == child->second.galaxy.parent) {
                parents.push_back({ formID, entry.galaxy, entry.editorID });
            }
        }
        std::ranges::sort(parents, {}, &IndexedBody::formID);
        return parents;
    }

    std::vector<IndexedBody> StationOrbitals(std::uint32_t a_cellFormID)
    {
        std::lock_guard lock{ g_mutex };
        if (const auto found = g_stationOrbitals.find(a_cellFormID);
            found != g_stationOrbitals.end())
            return found->second;
        return {};
    }

    std::optional<std::uint32_t> LookupSystemRoot(std::uint32_t a_formID)
    {
        std::lock_guard lock{ g_mutex };
        if (const auto found = g_systemRoots.find(a_formID);
            found != g_systemRoots.end())
            return found->second;
        return std::nullopt;
    }

    bool IsStationBase(std::uint32_t a_formID)
    {
        std::lock_guard lock{ g_mutex };
        return g_stationBases.contains(a_formID);
    }

    std::vector<StationTarget> StationTargets(std::uint32_t a_cellFormID)
    {
        std::lock_guard lock{ g_mutex };
        if (const auto found = g_stationTargets.find(a_cellFormID);
            found != g_stationTargets.end())
            return found->second;
        return {};
    }
}
