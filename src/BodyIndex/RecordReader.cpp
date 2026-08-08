#include "BodyIndex/RecordReader.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <format>
#include <ranges>
#include <string_view>
#include <utility>

namespace CFS::BodyIndex::RecordReader
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

        struct MapMarkerPlacement
        {
            std::uint32_t cellFormID{ 0 };
            std::uint32_t referenceFormID{ 0 };
        };

        constexpr std::uint32_t EncodeRuntimeFormID(std::uint32_t a_local,
            PluginTier a_tier, std::uint16_t a_index)
        {
            switch (a_tier) {
            case PluginTier::kSmall:
                return 0xFE000000u | (static_cast<std::uint32_t>(a_index) << 12) |
                       (a_local & 0x00000FFFu);
            case PluginTier::kMedium:
                return 0xFD000000u | (static_cast<std::uint32_t>(a_index) << 16) |
                       (a_local & 0x0000FFFFu);
            default:
                return (static_cast<std::uint32_t>(a_index) << 24) |
                       (a_local & 0x00FFFFFFu);
            }
        }
        static_assert(EncodeRuntimeFormID(0x01001234u, PluginTier::kFull, 5) ==
                      0x05001234u);
        static_assert(EncodeRuntimeFormID(0x01001234u, PluginTier::kMedium, 3) ==
                      0xFD031234u);
        static_assert(EncodeRuntimeFormID(0x01001234u, PluginTier::kSmall, 2) ==
                      0xFE002234u);

        std::size_t BoundedLength(const char* a_text, std::size_t a_limit)
        {
            return static_cast<std::size_t>(
                std::find(a_text, a_text + a_limit, '\0') - a_text);
        }

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

        std::string Fold(std::string a_text)
        {
            std::ranges::transform(a_text, a_text.begin(), [](unsigned char a_ch) {
                return static_cast<char>(std::tolower(a_ch));
            });
            return a_text;
        }

        std::uint32_t ResolveFormID(std::uint32_t a_local,
            const std::vector<PluginInfo>& a_masters, const PluginInfo& a_self)
        {
            const auto slot = static_cast<std::size_t>(a_local >> 24);
            const auto& owner = slot < a_masters.size() ? a_masters[slot] : a_self;
            return EncodeRuntimeFormID(a_local, owner.tier, owner.index);
        }

        bool ReadMasters(const std::filesystem::path& a_path,
            std::vector<std::string>& a_out)
        {
            std::ifstream file{ a_path, std::ios::binary };
            RecordHeader header{};
            if (!ReadExact(file, &header, sizeof(header)) ||
                std::memcmp(header.signature, "TES4", 4) != 0)
                return false;
            std::vector<std::byte> data(header.dataSize);
            if (header.dataSize && !ReadExact(file, data.data(), data.size()))
                return false;
            ForEachSubrecord(data.data(), data.size(), [&](std::string_view a_sig,
                const std::byte* a_payload, std::size_t a_length) {
                if (a_sig == "MAST" && a_length > 1) {
                    const auto* chars = reinterpret_cast<const char*>(a_payload);
                    a_out.emplace_back(chars, BoundedLength(chars, a_length));
                }
                return true;
            });
            return true;
        }

        std::uint64_t SeekGroup(std::ifstream& a_file, const char (&a_label)[5])
        {
            while (a_file) {
                const auto position = a_file.tellg();
                if (position < 0)
                    return 0;
                const auto start = static_cast<std::uint64_t>(position);
                RecordHeader group{};
                if (!ReadExact(a_file, &group, sizeof(group)) ||
                    std::memcmp(group.signature, "GRUP", 4) != 0)
                    return 0;
                if (group.dataSize < sizeof(RecordHeader))
                    return 0;
                if (std::memcmp(&group.flagsOrLabel, a_label, 4) == 0)
                    return start + group.dataSize;
                a_file.seekg(static_cast<std::streamoff>(start + group.dataSize),
                    std::ios::beg);
            }
            return 0;
        }

        void ParseStationBases(const std::filesystem::path& a_dataRoot,
            const PluginInfo& a_plugin, const std::vector<PluginInfo>& a_masters,
            std::unordered_set<std::uint32_t>& a_out)
        {
            std::ifstream file{ a_dataRoot / a_plugin.name, std::ios::binary };
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
            while (file) {
                const auto position = file.tellg();
                if (position < 0 ||
                    static_cast<std::uint64_t>(position) + sizeof(RecordHeader) > end)
                    break;
                const auto start = static_cast<std::uint64_t>(position);
                RecordHeader record{};
                if (!ReadExact(file, &record, sizeof(record)))
                    break;
                if (std::memcmp(record.signature, "GRUP", 4) == 0) {
                    if (record.dataSize < sizeof(RecordHeader) ||
                        start + record.dataSize > end)
                        break;
                    file.seekg(static_cast<std::streamoff>(start + record.dataSize),
                        std::ios::beg);
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
                    for (std::size_t offset = 0;
                         offset + sizeof(std::uint32_t) <= a_length;
                         offset += sizeof(std::uint32_t)) {
                        std::uint32_t localKeyword = 0;
                        std::memcpy(&localKeyword, a_payload + offset,
                            sizeof(localKeyword));
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
            std::uint32_t a_currentCell, bool a_placedChild,
            const PluginInfo& a_plugin, const std::vector<PluginInfo>& a_masters,
            const std::unordered_set<std::uint32_t>& a_stationBases,
            std::unordered_map<std::uint32_t, StationPlacement>& a_out,
            std::unordered_map<std::uint32_t, MapMarkerPlacement>& a_mapMarkers,
            std::unordered_map<std::uint32_t, std::string>& a_cellEditorIDs)
        {
            std::vector<std::byte> raw;
            std::vector<std::byte> scratch;
            while (a_file) {
                const auto position = a_file.tellg();
                if (position < 0 ||
                    static_cast<std::uint64_t>(position) + sizeof(RecordHeader) > a_end)
                    return;
                const auto start = static_cast<std::uint64_t>(position);
                RecordHeader record{};
                if (!ReadExact(a_file, &record, sizeof(record)))
                    return;

                if (std::memcmp(record.signature, "GRUP", 4) == 0) {
                    if (record.dataSize < sizeof(RecordHeader) ||
                        start + record.dataSize > a_end)
                        return;
                    const auto groupEnd = start + record.dataSize;
                    const auto groupType = record.formID;

                    auto nextCell = a_currentCell;
                    auto nextPlacedChild = a_placedChild;
                    if (groupType == kCellChildren) {
                        nextCell = ResolveFormID(record.flagsOrLabel, a_masters, a_plugin);
                        nextPlacedChild = false;
                    } else if (groupType == kCellPersistentChildren ||
                               groupType == kCellTemporaryChildren) {
                        nextCell = ResolveFormID(record.flagsOrLabel, a_masters, a_plugin);
                        nextPlacedChild = true;
                    }
                    ParseStationReferenceGroup(a_file, groupEnd, nextCell,
                        nextPlacedChild, a_plugin, a_masters, a_stationBases, a_out,
                        a_mapMarkers, a_cellEditorIDs);
                    a_file.seekg(static_cast<std::streamoff>(groupEnd), std::ios::beg);
                    continue;
                }

                if (start + sizeof(RecordHeader) + record.dataSize > a_end)
                    return;
                const bool isCell = std::memcmp(record.signature, "CELL", 4) == 0;
                const bool isPlacedReference = a_currentCell && a_placedChild &&
                    std::memcmp(record.signature, "REFR", 4) == 0;
                if (!isCell && !isPlacedReference) {
                    a_file.seekg(record.dataSize, std::ios::cur);
                    continue;
                }

                raw.resize(record.dataSize);
                if (record.dataSize && !ReadExact(a_file, raw.data(), raw.size()))
                    return;
                if (isCell) {
                    const auto cellID = ResolveFormID(record.formID, a_masters, a_plugin);
                    if ((record.flagsOrLabel & kDeleted) != 0) {
                        a_cellEditorIDs.erase(cellID);
                        std::erase_if(a_out, [cellID](const auto& a_entry) {
                            return a_entry.second.cellFormID == cellID;
                        });
                        std::erase_if(a_mapMarkers, [cellID](const auto& a_entry) {
                            return a_entry.second.cellFormID == cellID;
                        });
                        continue;
                    }
                    std::string editorID;
                    const auto body = RecordBody(record.flagsOrLabel, raw, scratch);
                    ForEachSubrecord(body.data(), body.size(), [&](std::string_view a_sig,
                        const std::byte* a_payload, std::size_t a_length) {
                        if (a_sig == "EDID" && a_length > 1) {
                            const auto* chars = reinterpret_cast<const char*>(a_payload);
                            editorID.assign(chars, BoundedLength(chars, a_length));
                            return false;
                        }
                        return true;
                    });
                    if (!editorID.empty())
                        a_cellEditorIDs.insert_or_assign(cellID, std::move(editorID));
                    continue;
                }

                const auto referenceID = ResolveFormID(record.formID, a_masters, a_plugin);
                if ((record.flagsOrLabel & kDeleted) != 0) {
                    a_out.erase(referenceID);
                    a_mapMarkers.erase(referenceID);
                    continue;
                }

                std::uint32_t baseID = 0;
                std::string editorID;
                bool isMapMarker = false;
                const auto body = RecordBody(record.flagsOrLabel, raw, scratch);
                ForEachSubrecord(body.data(), body.size(), [&](std::string_view a_sig,
                    const std::byte* a_payload, std::size_t a_length) {
                    if (a_sig == "NAME" && a_length >= sizeof(std::uint32_t)) {
                        std::uint32_t localBase = 0;
                        std::memcpy(&localBase, a_payload, sizeof(localBase));
                        baseID = ResolveFormID(localBase, a_masters, a_plugin);
                    } else if (a_sig == "EDID" && a_length > 1) {
                        const auto* chars = reinterpret_cast<const char*>(a_payload);
                        editorID.assign(chars, BoundedLength(chars, a_length));
                    } else if (a_sig == "XMRK") {
                        isMapMarker = true;
                    }
                    return true;
                });

                if (isMapMarker) {
                    a_mapMarkers.insert_or_assign(referenceID, MapMarkerPlacement{
                        .cellFormID = a_currentCell,
                        .referenceFormID = referenceID,
                    });
                } else {
                    a_mapMarkers.erase(referenceID);
                }

                if (!baseID || !a_stationBases.contains(baseID)) {
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

        void ParseStationPlacements(const std::filesystem::path& a_dataRoot,
            const PluginInfo& a_plugin, const std::vector<PluginInfo>& a_masters,
            const std::unordered_set<std::uint32_t>& a_stationBases,
            std::unordered_map<std::uint32_t, StationPlacement>& a_out,
            std::unordered_map<std::uint32_t, MapMarkerPlacement>& a_mapMarkers,
            std::unordered_map<std::uint32_t, std::string>& a_cellEditorIDs,
            std::vector<Diagnostic>& a_diagnostics)
        {
            std::ifstream file{ a_dataRoot / a_plugin.name, std::ios::binary };
            RecordHeader header{};
            if (!ReadExact(file, &header, sizeof(header)) ||
                std::memcmp(header.signature, "TES4", 4) != 0)
                return;
            file.seekg(header.dataSize, std::ios::cur);

            const auto before = a_out.size();
            while (file) {
                const auto position = file.tellg();
                if (position < 0)
                    break;
                const auto start = static_cast<std::uint64_t>(position);
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
                        a_masters, a_stationBases, a_out, a_mapMarkers,
                        a_cellEditorIDs);
                }
                file.seekg(static_cast<std::streamoff>(groupEnd), std::ios::beg);
            }
            if (a_out.size() > before) {
                a_diagnostics.push_back({ false,
                    std::format("{} placed station references added/updated from {}",
                        a_out.size() - before, a_plugin.name) });
            }
        }

        void ParseSystemRoots(const std::filesystem::path& a_dataRoot,
            const PluginInfo& a_plugin, const std::vector<PluginInfo>& a_masters,
            const LiveFormPredicate& a_isLiveForm,
            std::unordered_map<std::uint32_t, std::uint32_t>& a_out,
            std::vector<Diagnostic>& a_diagnostics)
        {
            std::ifstream file{ a_dataRoot / a_plugin.name, std::ios::binary };
            RecordHeader header{};
            if (!ReadExact(file, &header, sizeof(header)) ||
                std::memcmp(header.signature, "TES4", 4) != 0)
                return;
            file.seekg(header.dataSize, std::ios::cur);
            const auto end = SeekGroup(file, "STDT");
            if (!end)
                return;

            std::vector<std::byte> raw;
            std::vector<std::byte> scratch;
            std::size_t count = 0;
            while (file) {
                const auto position = file.tellg();
                if (position < 0 ||
                    static_cast<std::uint64_t>(position) + sizeof(RecordHeader) >= end)
                    break;
                const auto start = static_cast<std::uint64_t>(position);
                RecordHeader record{};
                if (!ReadExact(file, &record, sizeof(record)))
                    break;
                if (std::memcmp(record.signature, "GRUP", 4) == 0) {
                    file.seekg(static_cast<std::streamoff>(start + record.dataSize),
                        std::ios::beg);
                    continue;
                }
                raw.resize(record.dataSize);
                if (record.dataSize && !ReadExact(file, raw.data(), raw.size()))
                    break;
                if (std::memcmp(record.signature, "STDT", 4) != 0)
                    continue;

                const auto runtimeID = ResolveFormID(record.formID, a_masters, a_plugin);
                if ((record.flagsOrLabel & kDeleted) != 0) {
                    a_out.erase(runtimeID);
                    continue;
                }

                std::uint32_t systemID = 0;
                bool haveSystemID = false;
                const auto body = RecordBody(record.flagsOrLabel, raw, scratch);
                ForEachSubrecord(body.data(), body.size(), [&](std::string_view a_sig,
                    const std::byte* a_payload, std::size_t a_length) {
                    if (a_sig == "DNAM" && a_length >= sizeof(systemID)) {
                        std::memcpy(&systemID, a_payload, sizeof(systemID));
                        haveSystemID = true;
                        return false;
                    }
                    return true;
                });
                if (!haveSystemID || !a_isLiveForm ||
                    !a_isLiveForm(runtimeID, LiveFormKind::kStarData))
                    continue;
                a_out.insert_or_assign(runtimeID, systemID);
                ++count;
            }
            if (count) {
                a_diagnostics.push_back({ false,
                    std::format("{} STDT system roots from {}", count, a_plugin.name) });
            }
        }

        void ParsePlanetData(const std::filesystem::path& a_dataRoot,
            const PluginInfo& a_plugin, const std::vector<PluginInfo>& a_masters,
            const LiveFormPredicate& a_isLiveForm,
            std::unordered_map<std::uint32_t, Entry>& a_out,
            std::vector<Diagnostic>& a_diagnostics)
        {
            std::ifstream file{ a_dataRoot / a_plugin.name, std::ios::binary };
            RecordHeader header{};
            if (!ReadExact(file, &header, sizeof(header)) ||
                std::memcmp(header.signature, "TES4", 4) != 0)
                return;
            file.seekg(header.dataSize, std::ios::cur);
            const auto end = SeekGroup(file, "PNDT");
            if (!end)
                return;

            std::vector<std::byte> raw;
            std::vector<std::byte> scratch;
            std::size_t count = 0;
            while (file) {
                const auto position = file.tellg();
                if (position < 0 ||
                    static_cast<std::uint64_t>(position) + sizeof(RecordHeader) >= end)
                    break;
                const auto start = static_cast<std::uint64_t>(position);
                RecordHeader record{};
                if (!ReadExact(file, &record, sizeof(record)))
                    break;
                if (std::memcmp(record.signature, "GRUP", 4) == 0) {
                    file.seekg(static_cast<std::streamoff>(start + record.dataSize),
                        std::ios::beg);
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
                        entry.editorID.assign(chars, BoundedLength(chars, a_length));
                    } else if (a_sig == "DNAM" && a_length >= 5) {
                        std::uint32_t stringBytes = 0;
                        std::memcpy(&stringBytes, a_payload, sizeof(stringBytes));
                        if (stringBytes > 1 &&
                            sizeof(stringBytes) + stringBytes <= a_length) {
                            const auto* chars = reinterpret_cast<const char*>(
                                a_payload + sizeof(stringBytes));
                            const auto textLength = BoundedLength(chars, stringBytes);
                            if (textLength + 1 == stringBytes)
                                entry.spaceCellEditorID.assign(chars, textLength);
                        }
                    } else if (a_sig == "GNAM" && a_length >= 12) {
                        std::uint32_t values[3]{};
                        std::memcpy(values, a_payload, sizeof(values));
                        entry.galaxy = { values[0], values[1], values[2] };
                        haveGalaxy = true;
                    }
                    return true;
                });
                if (!haveGalaxy)
                    continue;
                const auto runtimeID = ResolveFormID(record.formID, a_masters, a_plugin);
                if (!a_isLiveForm ||
                    !a_isLiveForm(runtimeID, LiveFormKind::kPlanetData))
                    continue;
                a_out.insert_or_assign(runtimeID, std::move(entry));
                ++count;
            }
            if (count) {
                a_diagnostics.push_back({ false,
                    std::format("{} PNDT records from {}", count, a_plugin.name) });
            }
        }
    }

    Result ReadLoadOrder(const std::filesystem::path& a_dataRoot,
        std::span<const PluginInfo> a_plugins,
        const LiveFormPredicate& a_isLiveForm)
    {
        Result result;
        std::unordered_map<std::string, PluginInfo> pluginByName;
        for (const auto& plugin : a_plugins)
            pluginByName[Fold(plugin.name)] = plugin;

        std::vector<PluginContext> contexts;
        for (const auto& plugin : a_plugins) {
            std::vector<std::string> masterNames;
            if (!ReadMasters(a_dataRoot / plugin.name, masterNames)) {
                result.diagnostics.push_back({ true,
                    std::format("could not read {}", plugin.name) });
                continue;
            }
            PluginContext context{ .plugin = plugin };
            for (const auto& master : masterNames) {
                const auto found = pluginByName.find(Fold(master));
                if (found == pluginByName.end()) {
                    result.diagnostics.push_back({ true,
                        std::format("{} master '{}' is not active; records using it will not resolve",
                            plugin.name, master) });
                    context.masters.push_back({});
                } else {
                    context.masters.push_back(found->second);
                }
            }
            contexts.push_back(std::move(context));
        }

        std::unordered_map<std::uint32_t, StationPlacement> stationPlacements;
        std::unordered_map<std::uint32_t, MapMarkerPlacement> mapMarkerPlacements;
        std::unordered_map<std::uint32_t, std::string> cellEditorIDs;

        for (const auto& context : contexts) {
            ParseStationBases(a_dataRoot, context.plugin, context.masters,
                result.stationBases);
        }
        for (const auto& context : contexts) {
            ParsePlanetData(a_dataRoot, context.plugin, context.masters, a_isLiveForm,
                result.entries, result.diagnostics);
            ParseSystemRoots(a_dataRoot, context.plugin, context.masters, a_isLiveForm,
                result.systemRoots, result.diagnostics);
            ParseStationPlacements(a_dataRoot, context.plugin, context.masters,
                result.stationBases, stationPlacements, mapMarkerPlacements,
                cellEditorIDs, result.diagnostics);
        }

        for (auto& [referenceID, placement] : stationPlacements) {
            (void)referenceID;
            result.stationTargets[placement.cellFormID].push_back(
                std::move(placement.target));
        }
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>
            mapMarkersByCell;
        for (const auto& [referenceID, placement] : mapMarkerPlacements) {
            (void)referenceID;
            mapMarkersByCell[placement.cellFormID].push_back(
                placement.referenceFormID);
        }
        for (auto& [cellID, markers] : mapMarkersByCell) {
            (void)cellID;
            std::ranges::sort(markers);
            markers.erase(std::unique(markers.begin(), markers.end()), markers.end());
        }
        for (auto& [cellID, targets] : result.stationTargets) {
            std::ranges::sort(targets, {}, &StationTarget::referenceFormID);
            targets.erase(std::unique(targets.begin(), targets.end(),
                [](const StationTarget& a_left, const StationTarget& a_right) {
                    return a_left.referenceFormID == a_right.referenceFormID;
                }), targets.end());
            if (const auto markers = mapMarkersByCell.find(cellID);
                markers != mapMarkersByCell.end() && markers->second.size() == 1) {
                for (auto& target : targets)
                    target.courseFormID = markers->second.front();
                result.stationCourseMarkerCount += targets.size();
            }
            result.stationReferenceCount += targets.size();
        }

        std::unordered_map<std::string, std::vector<IndexedBody>>
            orbitalsByCellEditorID;
        for (const auto& [formID, entry] : result.entries) {
            if (entry.spaceCellEditorID.empty())
                continue;
            orbitalsByCellEditorID[Fold(entry.spaceCellEditorID)].push_back({
                formID, entry.galaxy, entry.editorID,
            });
        }
        for (const auto& [cellID, targets] : result.stationTargets) {
            (void)targets;
            const auto cellEditor = cellEditorIDs.find(cellID);
            if (cellEditor == cellEditorIDs.end())
                continue;
            const auto found = orbitalsByCellEditorID.find(Fold(cellEditor->second));
            if (found == orbitalsByCellEditorID.end())
                continue;
            auto& orbitals = result.stationOrbitals[cellID];
            orbitals = found->second;
            std::ranges::sort(orbitals, {}, &IndexedBody::formID);
            orbitals.erase(std::unique(orbitals.begin(), orbitals.end(),
                [](const IndexedBody& a_left, const IndexedBody& a_right) {
                    return a_left.formID == a_right.formID;
                }), orbitals.end());
            result.stationOrbitalCount += orbitals.size();
        }

        return result;
    }
}
