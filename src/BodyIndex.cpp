#include "BodyIndex.h"

#include "RE/Starfield.h"

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CFS::BodyIndex
{
    namespace
    {
        constexpr std::uint32_t kDeleted = 0x00000020;
        constexpr std::uint32_t kCompressed = 0x00040000;
        constexpr std::uint32_t kCellChildren = 6;
        constexpr std::uint32_t kCellPersistentChildren = 8;
        constexpr std::uint32_t kCellTemporaryChildren = 9;
        constexpr std::uint32_t kStarstationKeyword = 0x003402A3;

        struct RecordHeader
        {
            char signature[4];
            std::uint32_t dataSize;
            std::uint32_t flagsOrLabel;
            std::uint32_t formID;
            std::uint32_t unk10;
            std::uint32_t unk14;
        };
        static_assert(sizeof(RecordHeader) == 24);

        struct PluginInfo
        {
            enum class Tier : std::uint8_t
            {
                kFull,
                kSmall,
                kMedium,
            };

            std::string name;
            Tier tier{ Tier::kFull };
            std::uint16_t index{ 0 };
        };

        struct PluginContext
        {
            PluginInfo plugin;
            std::vector<PluginInfo> masters;
        };

        struct StationPlacement
        {
            std::uint32_t cellFormID{ 0 };
            StationTarget target;
        };

        constexpr std::uint32_t EncodeRuntimeFormID(std::uint32_t a_local,
            PluginInfo::Tier a_tier, std::uint16_t a_index)
        {
            switch (a_tier) {
            case PluginInfo::Tier::kSmall:
                return 0xFE000000u | (static_cast<std::uint32_t>(a_index) << 12) |
                       (a_local & 0x00000FFFu);
            case PluginInfo::Tier::kMedium:
                return 0xFD000000u | (static_cast<std::uint32_t>(a_index) << 16) |
                       (a_local & 0x0000FFFFu);
            default:
                return (static_cast<std::uint32_t>(a_index) << 24) |
                       (a_local & 0x00FFFFFFu);
            }
        }
        static_assert(EncodeRuntimeFormID(0x01001234u, PluginInfo::Tier::kFull, 5) ==
                      0x05001234u);
        static_assert(EncodeRuntimeFormID(0x01001234u, PluginInfo::Tier::kMedium, 3) ==
                      0xFD031234u);
        static_assert(EncodeRuntimeFormID(0x01001234u, PluginInfo::Tier::kSmall, 2) ==
                      0xFE002234u);

        std::mutex g_mutex;
        std::unordered_map<std::uint32_t, Entry> g_entries;
        std::unordered_set<std::uint32_t> g_stationBases;
        std::unordered_map<std::uint32_t, std::vector<StationTarget>> g_stationTargets;
        std::atomic<bool> g_started{ false };
        std::atomic<bool> g_ready{ false };

        bool ReadExact(std::ifstream& a_file, void* a_out, std::size_t a_size)
        {
            a_file.read(static_cast<char*>(a_out), static_cast<std::streamsize>(a_size));
            return a_file.gcount() == static_cast<std::streamsize>(a_size);
        }

        template <class F>
        void ForEachSubrecord(const std::byte* a_data, std::size_t a_size, F&& a_fn)
        {
            std::size_t offset = 0;
            std::uint32_t extended = 0;
            while (offset + 6 <= a_size) {
                char signature[4];
                std::memcpy(signature, a_data + offset, 4);
                std::uint16_t size16 = 0;
                std::memcpy(&size16, a_data + offset + 4, 2);
                offset += 6;
                std::uint32_t size = size16;
                if (std::memcmp(signature, "XXXX", 4) == 0 && size == 4) {
                    if (offset + 4 > a_size)
                        return;
                    std::memcpy(&extended, a_data + offset, 4);
                    offset += 4;
                    continue;
                }
                if (extended != 0) {
                    size = extended;
                    extended = 0;
                }
                if (offset + size > a_size)
                    return;
                if (!a_fn(std::string_view{ signature, 4 }, a_data + offset, size))
                    return;
                offset += size;
            }
        }

        std::span<const std::byte> RecordBody(std::uint32_t a_flags,
            const std::vector<std::byte>& a_raw, std::vector<std::byte>& a_scratch)
        {
            if ((a_flags & kCompressed) == 0)
                return { a_raw.data(), a_raw.size() };
            if (a_raw.size() < 5)
                return {};
            std::uint32_t expanded = 0;
            std::memcpy(&expanded, a_raw.data(), 4);
            if (expanded == 0 || expanded > 64u * 1024u * 1024u)
                return {};
            a_scratch.resize(expanded);
            uLongf produced = expanded;
            if (::uncompress(reinterpret_cast<Bytef*>(a_scratch.data()), &produced,
                    reinterpret_cast<const Bytef*>(a_raw.data() + 4),
                    static_cast<uLong>(a_raw.size() - 4)) != Z_OK)
                return {};
            return { a_scratch.data(), produced };
        }

        std::vector<PluginInfo> CollectPlugins()
        {
            std::vector<PluginInfo> plugins;
            const auto handler = RE::TESDataHandler::GetSingleton();
            if (!handler)
                return plugins;

            const auto append = [&plugins](const auto& a_files, PluginInfo::Tier a_tier) {
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
                    if (!(name.ends_with(".esm") || name.ends_with(".esp") || name.ends_with(".esl")))
                        return false;
                    const auto index = a_tier == PluginInfo::Tier::kFull ?
                        static_cast<std::uint16_t>(file->compileIndex) : tierIndex;
                    plugins.push_back({ std::move(name), a_tier, index });
                    ++tierIndex;
                }
                return true;
            };

            if (!append(handler->compiledFileCollection.files, PluginInfo::Tier::kFull) ||
                !append(handler->compiledFileCollection.smallFiles, PluginInfo::Tier::kSmall) ||
                !append(handler->compiledFileCollection.mediumFiles, PluginInfo::Tier::kMedium)) {
                REX::WARN("[bodies] active plugin collection failed validation");
                return {};
            }

            REX::INFO("[bodies] active plugin tiers: full={} small={} medium={} total={}",
                handler->compiledFileCollection.files.size(),
                handler->compiledFileCollection.smallFiles.size(),
                handler->compiledFileCollection.mediumFiles.size(), plugins.size());
            return plugins;
        }

        std::uint32_t ResolveFormID(std::uint32_t a_local,
            const std::vector<PluginInfo>& a_masters, const PluginInfo& a_self)
        {
            const auto slot = static_cast<std::size_t>(a_local >> 24);
            const auto& owner = slot < a_masters.size() ? a_masters[slot] : a_self;
            return EncodeRuntimeFormID(a_local, owner.tier, owner.index);
        }

        bool ReadMasters(const std::filesystem::path& a_path, std::vector<std::string>& a_out)
        {
            std::ifstream file{ a_path, std::ios::binary };
            RecordHeader header{};
            if (!ReadExact(file, &header, sizeof(header)) || std::memcmp(header.signature, "TES4", 4) != 0)
                return false;
            std::vector<std::byte> data(header.dataSize);
            if (header.dataSize && !ReadExact(file, data.data(), data.size()))
                return false;
            ForEachSubrecord(data.data(), data.size(), [&](std::string_view a_sig,
                const std::byte* a_payload, std::size_t a_length) {
                if (a_sig == "MAST" && a_length > 1) {
                    const auto* chars = reinterpret_cast<const char*>(a_payload);
                    a_out.emplace_back(chars, ::strnlen(chars, a_length));
                }
                return true;
            });
            return true;
        }

        std::uint64_t SeekGroup(std::ifstream& a_file, const char (&a_label)[5])
        {
            while (a_file) {
                const auto start = static_cast<std::uint64_t>(a_file.tellg());
                RecordHeader group{};
                if (!ReadExact(a_file, &group, sizeof(group)) || std::memcmp(group.signature, "GRUP", 4) != 0)
                    return 0;
                if (std::memcmp(&group.flagsOrLabel, a_label, 4) == 0)
                    return start + group.dataSize;
                a_file.seekg(static_cast<std::streamoff>(start + group.dataSize), std::ios::beg);
            }
            return 0;
        }

        void ParseStationBases(const PluginInfo& a_plugin,
            const std::vector<PluginInfo>& a_masters,
            std::unordered_set<std::uint32_t>& a_out)
        {
            const auto path = std::filesystem::path{ "Data" } / a_plugin.name;
            std::ifstream file{ path, std::ios::binary };
            RecordHeader header{};
            if (!ReadExact(file, &header, sizeof(header)) ||
                std::memcmp(header.signature, "TES4", 4) != 0)
                return;
            file.seekg(header.dataSize, std::ios::cur);
            const auto end = SeekGroup(file, "GBFM");
            if (!end)
                return;

            std::vector<std::byte> raw;
            std::vector<std::byte> scratch;
            while (file &&
                   static_cast<std::uint64_t>(file.tellg()) + sizeof(RecordHeader) <= end) {
                const auto start = static_cast<std::uint64_t>(file.tellg());
                RecordHeader record{};
                if (!ReadExact(file, &record, sizeof(record)))
                    break;
                if (std::memcmp(record.signature, "GRUP", 4) == 0) {
                    if (record.dataSize < sizeof(RecordHeader) || start + record.dataSize > end)
                        break;
                    file.seekg(static_cast<std::streamoff>(start + record.dataSize), std::ios::beg);
                    continue;
                }
                if (start + sizeof(RecordHeader) + record.dataSize > end)
                    break;
                raw.resize(record.dataSize);
                if (record.dataSize && !ReadExact(file, raw.data(), raw.size()))
                    break;
                if (std::memcmp(record.signature, "GBFM", 4) != 0)
                    continue;

                bool isStation = false;
                const auto body = RecordBody(record.flagsOrLabel, raw, scratch);
                ForEachSubrecord(body.data(), body.size(), [&](std::string_view a_sig,
                    const std::byte* a_payload, std::size_t a_length) {
                    if (a_sig != "KWDA")
                        return true;
                    for (std::size_t offset = 0; offset + sizeof(std::uint32_t) <= a_length;
                         offset += sizeof(std::uint32_t)) {
                        std::uint32_t localKeyword = 0;
                        std::memcpy(&localKeyword, a_payload + offset, sizeof(localKeyword));
                        if (ResolveFormID(localKeyword, a_masters, a_plugin) ==
                            kStarstationKeyword) {
                            isStation = true;
                            return false;
                        }
                    }
                    return true;
                });

                const auto runtimeID = ResolveFormID(record.formID, a_masters, a_plugin);
                if ((record.flagsOrLabel & kDeleted) != 0 || !isStation)
                    a_out.erase(runtimeID);
                else
                    a_out.insert(runtimeID);
            }
        }

        void ParseStationReferenceGroup(std::ifstream& a_file, std::uint64_t a_end,
            std::uint32_t a_currentCell, bool a_persistent,
            const PluginInfo& a_plugin, const std::vector<PluginInfo>& a_masters,
            const std::unordered_set<std::uint32_t>& a_stationBases,
            std::unordered_map<std::uint32_t, StationPlacement>& a_out)
        {
            std::vector<std::byte> raw;
            std::vector<std::byte> scratch;
            while (a_file &&
                   static_cast<std::uint64_t>(a_file.tellg()) + sizeof(RecordHeader) <= a_end) {
                const auto start = static_cast<std::uint64_t>(a_file.tellg());
                RecordHeader record{};
                if (!ReadExact(a_file, &record, sizeof(record)))
                    return;

                if (std::memcmp(record.signature, "GRUP", 4) == 0) {
                    if (record.dataSize < sizeof(RecordHeader) || start + record.dataSize > a_end)
                        return;
                    const auto groupEnd = start + record.dataSize;
                    const auto groupType = record.formID;
                    if (groupType == kCellTemporaryChildren) {
                        a_file.seekg(static_cast<std::streamoff>(groupEnd), std::ios::beg);
                        continue;
                    }

                    auto nextCell = a_currentCell;
                    auto nextPersistent = a_persistent;
                    if (groupType == kCellChildren) {
                        nextCell = ResolveFormID(record.flagsOrLabel, a_masters, a_plugin);
                        nextPersistent = false;
                    } else if (groupType == kCellPersistentChildren) {
                        nextCell = ResolveFormID(record.flagsOrLabel, a_masters, a_plugin);
                        nextPersistent = true;
                    }
                    ParseStationReferenceGroup(a_file, groupEnd, nextCell, nextPersistent,
                        a_plugin, a_masters, a_stationBases, a_out);
                    a_file.seekg(static_cast<std::streamoff>(groupEnd), std::ios::beg);
                    continue;
                }

                if (start + sizeof(RecordHeader) + record.dataSize > a_end)
                    return;
                if (!a_currentCell || !a_persistent ||
                    std::memcmp(record.signature, "REFR", 4) != 0) {
                    a_file.seekg(record.dataSize, std::ios::cur);
                    continue;
                }

                raw.resize(record.dataSize);
                if (record.dataSize && !ReadExact(a_file, raw.data(), raw.size()))
                    return;
                const auto referenceID = ResolveFormID(record.formID, a_masters, a_plugin);
                if ((record.flagsOrLabel & kDeleted) != 0) {
                    a_out.erase(referenceID);
                    continue;
                }

                std::uint32_t baseID = 0;
                std::string editorID;
                const auto body = RecordBody(record.flagsOrLabel, raw, scratch);
                ForEachSubrecord(body.data(), body.size(), [&](std::string_view a_sig,
                    const std::byte* a_payload, std::size_t a_length) {
                    if (a_sig == "NAME" && a_length >= sizeof(std::uint32_t)) {
                        std::uint32_t localBase = 0;
                        std::memcpy(&localBase, a_payload, sizeof(localBase));
                        baseID = ResolveFormID(localBase, a_masters, a_plugin);
                    } else if (a_sig == "EDID" && a_length > 1) {
                        const auto* chars = reinterpret_cast<const char*>(a_payload);
                        editorID.assign(chars, ::strnlen(chars, a_length));
                    }
                    return true;
                });

                if (!baseID)
                    continue;
                if (!a_stationBases.contains(baseID)) {
                    a_out.erase(referenceID);
                    continue;
                }
                a_out.insert_or_assign(referenceID, StationPlacement{
                    .cellFormID = a_currentCell,
                    .target = {
                        .referenceFormID = referenceID,
                        .baseFormID = baseID,
                        .editorID = std::move(editorID),
                    },
                });
            }
        }

        void ParseStationPlacements(const PluginInfo& a_plugin,
            const std::vector<PluginInfo>& a_masters,
            const std::unordered_set<std::uint32_t>& a_stationBases,
            std::unordered_map<std::uint32_t, StationPlacement>& a_out)
        {
            const auto path = std::filesystem::path{ "Data" } / a_plugin.name;
            std::ifstream file{ path, std::ios::binary };
            RecordHeader header{};
            if (!ReadExact(file, &header, sizeof(header)) ||
                std::memcmp(header.signature, "TES4", 4) != 0)
                return;
            file.seekg(header.dataSize, std::ios::cur);

            const auto before = a_out.size();
            while (file) {
                const auto start = static_cast<std::uint64_t>(file.tellg());
                RecordHeader group{};
                if (!ReadExact(file, &group, sizeof(group)) ||
                    std::memcmp(group.signature, "GRUP", 4) != 0)
                    break;
                if (group.dataSize < sizeof(RecordHeader))
                    break;
                const auto groupEnd = start + group.dataSize;
                if (std::memcmp(&group.flagsOrLabel, "CELL", 4) == 0 ||
                    std::memcmp(&group.flagsOrLabel, "WRLD", 4) == 0) {
                    ParseStationReferenceGroup(file, groupEnd, 0, false, a_plugin,
                        a_masters, a_stationBases, a_out);
                }
                file.seekg(static_cast<std::streamoff>(groupEnd), std::ios::beg);
            }
            if (a_out.size() > before)
                REX::INFO("[bodies] {} persistent station references added/updated from {}",
                    a_out.size() - before, a_plugin.name);
        }

        void ParsePlugin(const PluginInfo& a_plugin, const std::vector<PluginInfo>& a_masters,
            std::unordered_map<std::uint32_t, Entry>& a_out)
        {
            const auto path = std::filesystem::path{ "Data" } / a_plugin.name;
            std::ifstream file{ path, std::ios::binary };
            RecordHeader header{};
            if (!ReadExact(file, &header, sizeof(header)) || std::memcmp(header.signature, "TES4", 4) != 0)
                return;
            file.seekg(header.dataSize, std::ios::cur);
            const auto end = SeekGroup(file, "PNDT");
            if (!end)
                return;

            std::vector<std::byte> raw;
            std::vector<std::byte> scratch;
            std::size_t count = 0;
            while (file && static_cast<std::uint64_t>(file.tellg()) + sizeof(RecordHeader) < end) {
                const auto start = static_cast<std::uint64_t>(file.tellg());
                RecordHeader record{};
                if (!ReadExact(file, &record, sizeof(record)))
                    break;
                if (std::memcmp(record.signature, "GRUP", 4) == 0) {
                    file.seekg(static_cast<std::streamoff>(start + record.dataSize), std::ios::beg);
                    continue;
                }
                raw.resize(record.dataSize);
                if (record.dataSize && !ReadExact(file, raw.data(), raw.size()))
                    break;
                if (std::memcmp(record.signature, "PNDT", 4) != 0)
                    continue;

                Entry entry;
                bool haveGalaxy = false;
                const auto body = RecordBody(record.flagsOrLabel, raw, scratch);
                ForEachSubrecord(body.data(), body.size(), [&](std::string_view a_sig,
                    const std::byte* a_payload, std::size_t a_length) {
                    if (a_sig == "EDID" && a_length > 1) {
                        const auto* chars = reinterpret_cast<const char*>(a_payload);
                        entry.editorID.assign(chars, ::strnlen(chars, a_length));
                    } else if (a_sig == "GNAM" && a_length >= 12) {
                        std::uint32_t values[3]{};
                        std::memcpy(values, a_payload, sizeof(values));
                        entry.galaxy = { values[0], values[1], values[2] };
                        haveGalaxy = true;
                        return false;
                    }
                    return true;
                });
                if (!haveGalaxy)
                    continue;
                const auto runtimeID = ResolveFormID(record.formID, a_masters, a_plugin);
                const auto form = RE::TESForm::LookupByID(runtimeID);
                if (!form || form->GetFormType() != RE::FormType::kPNDT)
                    continue;
                a_out.insert_or_assign(runtimeID, std::move(entry));
                ++count;
            }
            if (count)
                REX::INFO("[bodies] {} PNDT records from {}", count, a_plugin.name);
        }
    }

    void StartLoad()
    {
        if (g_started.exchange(true, std::memory_order_acq_rel))
            return;
        const auto plugins = CollectPlugins();
        std::thread{ [plugins] {
            const auto started = std::chrono::steady_clock::now();
            std::unordered_map<std::uint32_t, Entry> entries;
            std::unordered_set<std::uint32_t> stationBases;
            std::unordered_map<std::uint32_t, StationPlacement> stationPlacements;
            std::unordered_map<std::string, PluginInfo> pluginByName;
            const auto fold = [](std::string text) {
                std::ranges::transform(text, text.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                return text;
            };
            for (const auto& plugin : plugins)
                pluginByName[fold(plugin.name)] = plugin;

            std::vector<PluginContext> contexts;
            for (const auto& plugin : plugins) {
                std::vector<std::string> masterNames;
                if (!ReadMasters(std::filesystem::path{ "Data" } / plugin.name, masterNames)) {
                    REX::WARN("[bodies] could not read {}", plugin.name);
                    continue;
                }
                PluginContext context{ .plugin = plugin };
                for (const auto& master : masterNames) {
                    const auto found = pluginByName.find(fold(master));
                    if (found == pluginByName.end()) {
                        REX::WARN("[bodies] {} master '{}' is not active; records using it will not resolve",
                            plugin.name, master);
                        context.masters.push_back({});
                    } else {
                        context.masters.push_back(found->second);
                    }
                }
                contexts.push_back(std::move(context));
            }

            // Resolve the station base set first so references placed by any
            // active plugin can be validated against master or local GBFMs.
            for (const auto& context : contexts)
                ParseStationBases(context.plugin, context.masters, stationBases);
            for (const auto& context : contexts) {
                ParsePlugin(context.plugin, context.masters, entries);
                ParseStationPlacements(context.plugin, context.masters, stationBases,
                    stationPlacements);
            }

            std::unordered_map<std::uint32_t, std::vector<StationTarget>> stationTargets;
            for (auto& [referenceID, placement] : stationPlacements) {
                (void)referenceID;
                stationTargets[placement.cellFormID].push_back(std::move(placement.target));
            }
            std::size_t stationReferenceCount = 0;
            for (auto& [cellID, targets] : stationTargets) {
                (void)cellID;
                std::ranges::sort(targets, {}, &StationTarget::referenceFormID);
                targets.erase(std::unique(targets.begin(), targets.end(),
                    [](const StationTarget& a_left, const StationTarget& a_right) {
                        return a_left.referenceFormID == a_right.referenceFormID;
                    }), targets.end());
                stationReferenceCount += targets.size();
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            const auto count = entries.size();
            const auto stationBaseCount = stationBases.size();
            const auto stationCellCount = stationTargets.size();
            {
                std::lock_guard lock{ g_mutex };
                g_entries = std::move(entries);
                g_stationBases = std::move(stationBases);
                g_stationTargets = std::move(stationTargets);
            }
            g_ready.store(true, std::memory_order_release);
            REX::INFO("[bodies] load-order index ready: {} PNDT bodies, {} station bases, "
                      "{} persistent station refs in {} cells, {} ms",
                count, stationBaseCount, stationReferenceCount, stationCellCount, elapsed);
        } }.detach();
    }

    bool Ready() { return g_ready.load(std::memory_order_acquire); }

    std::size_t Size()
    {
        std::lock_guard lock{ g_mutex };
        return g_entries.size();
    }

    std::optional<Entry> Lookup(std::uint32_t a_formID)
    {
        std::lock_guard lock{ g_mutex };
        if (const auto found = g_entries.find(a_formID); found != g_entries.end())
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
