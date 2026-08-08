#include "BodyIndex/RecordReader.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Bytes = std::vector<std::byte>;
    using CFS::BodyIndex::RecordReader::LiveFormKind;
    using CFS::BodyIndex::RecordReader::PluginInfo;
    using CFS::BodyIndex::RecordReader::PluginTier;

    constexpr std::uint32_t kCompressed = 0x00040000;

    void Require(bool a_condition, std::string_view a_message)
    {
        if (!a_condition)
            throw std::runtime_error{ std::string{ a_message } };
    }

    void AppendSignature(Bytes& a_out, std::string_view a_signature)
    {
        Require(a_signature.size() == 4, "fixture signature must contain four bytes");
        for (const auto ch : a_signature)
            a_out.push_back(static_cast<std::byte>(ch));
    }

    void AppendU16(Bytes& a_out, std::uint16_t a_value)
    {
        a_out.push_back(static_cast<std::byte>(a_value & 0xFF));
        a_out.push_back(static_cast<std::byte>((a_value >> 8) & 0xFF));
    }

    void AppendU32(Bytes& a_out, std::uint32_t a_value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            a_out.push_back(static_cast<std::byte>((a_value >> shift) & 0xFF));
    }

    void Append(Bytes& a_out, std::span<const std::byte> a_bytes)
    {
        a_out.insert(a_out.end(), a_bytes.begin(), a_bytes.end());
    }

    Bytes U32Payload(std::initializer_list<std::uint32_t> a_values)
    {
        Bytes result;
        for (const auto value : a_values)
            AppendU32(result, value);
        return result;
    }

    Bytes StringPayload(std::string_view a_text)
    {
        Bytes result;
        result.reserve(a_text.size() + 1);
        for (const auto ch : a_text)
            result.push_back(static_cast<std::byte>(ch));
        result.push_back(std::byte{});
        return result;
    }

    Bytes Subrecord(std::string_view a_signature, const Bytes& a_payload,
        bool a_extended = false)
    {
        Bytes result;
        if (a_extended) {
            AppendSignature(result, "XXXX");
            AppendU16(result, 4);
            AppendU32(result, static_cast<std::uint32_t>(a_payload.size()));
            AppendSignature(result, a_signature);
            AppendU16(result, 0);
        } else {
            Require(a_payload.size() <= 0xFFFF, "ordinary fixture subrecord is too large");
            AppendSignature(result, a_signature);
            AppendU16(result, static_cast<std::uint16_t>(a_payload.size()));
        }
        Append(result, a_payload);
        return result;
    }

    Bytes Record(std::string_view a_signature, std::uint32_t a_formID,
        const Bytes& a_body, std::uint32_t a_flags = 0)
    {
        Bytes stored = a_body;
        if ((a_flags & kCompressed) != 0) {
            uLongf compressedSize = ::compressBound(static_cast<uLong>(a_body.size()));
            std::vector<Bytef> compressed(compressedSize);
            Require(::compress2(compressed.data(), &compressedSize,
                        reinterpret_cast<const Bytef*>(a_body.data()),
                        static_cast<uLong>(a_body.size()), Z_BEST_SPEED) == Z_OK,
                "zlib fixture compression failed");
            compressed.resize(compressedSize);
            stored.clear();
            AppendU32(stored, static_cast<std::uint32_t>(a_body.size()));
            stored.insert(stored.end(),
                reinterpret_cast<const std::byte*>(compressed.data()),
                reinterpret_cast<const std::byte*>(compressed.data() + compressed.size()));
        }

        Bytes result;
        AppendSignature(result, a_signature);
        AppendU32(result, static_cast<std::uint32_t>(stored.size()));
        AppendU32(result, a_flags);
        AppendU32(result, a_formID);
        AppendU32(result, 0);
        AppendU32(result, 0);
        Append(result, stored);
        return result;
    }

    Bytes Group(const std::array<std::byte, 4>& a_label, std::uint32_t a_type,
        const Bytes& a_children)
    {
        Bytes result;
        AppendSignature(result, "GRUP");
        AppendU32(result, static_cast<std::uint32_t>(24 + a_children.size()));
        Append(result, a_label);
        AppendU32(result, a_type);
        AppendU32(result, 0);
        AppendU32(result, 0);
        Append(result, a_children);
        return result;
    }

    Bytes Group(std::string_view a_label, std::uint32_t a_type,
        const Bytes& a_children)
    {
        Require(a_label.size() == 4, "fixture group label must contain four bytes");
        std::array<std::byte, 4> label{};
        std::ranges::transform(a_label, label.begin(),
            [](char a_ch) { return static_cast<std::byte>(a_ch); });
        return Group(label, a_type, a_children);
    }

    std::array<std::byte, 4> FormLabel(std::uint32_t a_formID)
    {
        return {
            static_cast<std::byte>(a_formID & 0xFF),
            static_cast<std::byte>((a_formID >> 8) & 0xFF),
            static_cast<std::byte>((a_formID >> 16) & 0xFF),
            static_cast<std::byte>((a_formID >> 24) & 0xFF),
        };
    }

    void AppendPart(Bytes& a_out, const Bytes& a_part)
    {
        Append(a_out, a_part);
    }

    Bytes PlanetBody(std::string_view a_editorID, std::string_view a_cellEditorID,
        std::array<std::uint32_t, 3> a_galaxy, bool a_exactDnam = true,
        bool a_extendedEditorID = false)
    {
        Bytes result;
        AppendPart(result, Subrecord("EDID", StringPayload(a_editorID),
            a_extendedEditorID));
        if (!a_cellEditorID.empty()) {
            auto text = StringPayload(a_cellEditorID);
            if (!a_exactDnam)
                text.push_back(std::byte{});
            Bytes dnam;
            AppendU32(dnam, static_cast<std::uint32_t>(text.size()));
            Append(dnam, text);
            AppendPart(result, Subrecord("DNAM", dnam));
        }
        AppendPart(result, Subrecord("GNAM",
            U32Payload({ a_galaxy[0], a_galaxy[1], a_galaxy[2] })));
        return result;
    }

    Bytes StationCell(std::uint32_t a_cellID, std::uint32_t a_stationReferenceID,
        std::initializer_list<std::uint32_t> a_markerReferenceIDs,
        std::string_view a_editorID)
    {
        Bytes result = Record("CELL", a_cellID,
            Subrecord("EDID", StringPayload(a_editorID)));

        Bytes placed;
        Bytes physical;
        AppendPart(physical, Subrecord("NAME", U32Payload({ 0x00000100 })));
        AppendPart(physical, Subrecord("EDID", StringPayload("FixtureStationRef")));
        AppendPart(placed, Record("REFR", a_stationReferenceID, physical));
        for (const auto markerID : a_markerReferenceIDs)
            AppendPart(placed, Record("REFR", markerID, Subrecord("XMRK", {})));

        const auto placedGroup = Group(FormLabel(a_cellID), 8, placed);
        AppendPart(result, Group(FormLabel(a_cellID), 6, placedGroup));
        return result;
    }

    Bytes PluginFile(const Bytes& a_tes4Body, const std::vector<Bytes>& a_groups)
    {
        auto result = Record("TES4", 0, a_tes4Body);
        for (const auto& group : a_groups)
            AppendPart(result, group);
        return result;
    }

    void WriteFile(const std::filesystem::path& a_path, const Bytes& a_bytes)
    {
        std::ofstream file{ a_path, std::ios::binary };
        Require(static_cast<bool>(file), "could not create fixture plugin");
        file.write(reinterpret_cast<const char*>(a_bytes.data()),
            static_cast<std::streamsize>(a_bytes.size()));
        Require(static_cast<bool>(file), "could not write fixture plugin");
    }

    void RunRecordReaderTests(const std::filesystem::path& a_dataRoot)
    {
        Bytes stationBase;
        AppendPart(stationBase,
            Subrecord("KWDA", U32Payload({ 0x003402A3 })));
        Bytes gbfmGroup = Group("GBFM", 0,
            Record("GBFM", 0x00000100, stationBase));

        Bytes planets;
        AppendPart(planets, Record("PNDT", 0x00000400,
            PlanetBody("FixtureOrbital", "StationCell", { 7, 0, 1 })));
        AppendPart(planets, Record("PNDT", 0x00000410,
            PlanetBody("MasterBeforeOverride", "ExactCell", { 7, 0, 2 })));
        AppendPart(planets, Record("PNDT", 0x00000411,
            PlanetBody("InvalidDnam", "InvalidCell", { 7, 0, 3 }, false)));
        const std::string extendedEditorID(70000, 'X');
        AppendPart(planets, Record("PNDT", 0x00000420,
            PlanetBody(extendedEditorID, {}, { 7, 0, 4 }, true, true)));
        AppendPart(planets, Record("PNDT", 0x00000430,
            PlanetBody("CompressedBody", {}, { 7, 0, 5 }), kCompressed));
        AppendPart(planets, Record("PNDT", 0x00000440,
            PlanetBody("RejectedBody", {}, { 7, 0, 6 })));
        const auto pndtGroup = Group("PNDT", 0, planets);

        Bytes stars;
        AppendPart(stars, Record("STDT", 0x00000800,
            Subrecord("DNAM", U32Payload({ 77 }))));
        AppendPart(stars, Record("STDT", 0x00000801,
            Subrecord("DNAM", U32Payload({ 88 }))));
        const auto stdtGroup = Group("STDT", 0, stars);

        Bytes cells;
        AppendPart(cells, StationCell(0x00000200, 0x00000300,
            { 0x00000301 }, "StationCell"));
        AppendPart(cells, StationCell(0x00000210, 0x00000310,
            { 0x00000311, 0x00000312 }, "AmbiguousStationCell"));
        const auto cellGroup = Group("CELL", 0, cells);

        WriteFile(a_dataRoot / "Starfield.esm",
            PluginFile({}, { gbfmGroup, pndtGroup, stdtGroup, cellGroup }));

        Bytes patchHeader = Subrecord("MAST", StringPayload("STARFIELD.ESM"));
        Bytes patchPlanets;
        AppendPart(patchPlanets, Record("PNDT", 0x00000410,
            PlanetBody("PatchOverride", "ExactCell", { 7, 0, 2 })));
        AppendPart(patchPlanets, Record("PNDT", 0x01000500,
            PlanetBody("PatchLocal", {}, { 7, 0, 7 })));
        WriteFile(a_dataRoot / "Patch.esm",
            PluginFile(patchHeader, { Group("PNDT", 0, patchPlanets) }));

        WriteFile(a_dataRoot / "Medium.esm", PluginFile({}, {
            Group("PNDT", 0, Record("PNDT", 0x01000600,
                PlanetBody("MediumLocal", {}, { 8, 0, 1 }))),
        }));
        WriteFile(a_dataRoot / "Small.esl", PluginFile({}, {
            Group("PNDT", 0, Record("PNDT", 0x01000678,
                PlanetBody("SmallLocal", {}, { 9, 0, 1 }))),
        }));

        const std::vector<PluginInfo> plugins{
            { "Starfield.esm", PluginTier::kFull, 0 },
            { "Patch.esm", PluginTier::kFull, 5 },
            { "Medium.esm", PluginTier::kMedium, 3 },
            { "Small.esl", PluginTier::kSmall, 2 },
        };
        std::size_t planetChecks = 0;
        std::size_t starChecks = 0;
        const auto result = CFS::BodyIndex::RecordReader::ReadLoadOrder(a_dataRoot,
            plugins, [&](std::uint32_t a_formID, LiveFormKind a_kind) {
                if (a_kind == LiveFormKind::kPlanetData) {
                    ++planetChecks;
                    return a_formID != 0x00000440;
                }
                ++starChecks;
                return a_formID != 0x00000801;
            });

        Require(planetChecks == 10, "live PNDT predicate did not see every parsed record");
        Require(starChecks == 2, "live STDT predicate did not see every parsed record");
        Require(!result.entries.contains(0x00000440),
            "live-form predicate did not reject PNDT fixture");
        Require(!result.systemRoots.contains(0x00000801),
            "live-form predicate did not reject STDT fixture");
        Require(result.systemRoots.at(0x00000800) == 77,
            "STDT DNAM system root was not decoded");

        Require(result.entries.at(0x00000410).editorID == "PatchOverride",
            "full-master override did not remap to the master's runtime FormID");
        Require(result.entries.at(0x05000500).editorID == "PatchLocal",
            "full-plugin local FormID remapping failed");
        Require(result.entries.at(0xFD030600).editorID == "MediumLocal",
            "medium-plugin FormID remapping failed");
        Require(result.entries.at(0xFE002678).editorID == "SmallLocal",
            "small-plugin FormID remapping failed");

        Require(result.entries.at(0x00000400).spaceCellEditorID == "StationCell",
            "exact DNAM length rule rejected a valid terminated string");
        Require(result.entries.at(0x00000411).spaceCellEditorID.empty(),
            "DNAM length rule accepted bytes beyond the first terminator");
        Require(result.entries.at(0x00000420).editorID.size() ==
                extendedEditorID.size(),
            "XXXX extended-size EDID was not decoded exactly");
        Require(result.entries.at(0x00000430).editorID == "CompressedBody",
            "zlib-compressed record body was not decoded");

        Require(result.stationBases.contains(0x00000100),
            "IsStarstation GBFM keyword remapping failed");
        const auto& exactTargets = result.stationTargets.at(0x00000200);
        Require(exactTargets.size() == 1 &&
                exactTargets.front().referenceFormID == 0x00000300 &&
                exactTargets.front().baseFormID == 0x00000100 &&
                exactTargets.front().courseFormID == 0x00000301,
            "unique CELL/XMRK station course link was not retained");
        const auto& ambiguousTargets = result.stationTargets.at(0x00000210);
        Require(ambiguousTargets.size() == 1 &&
                ambiguousTargets.front().courseFormID == 0,
            "ambiguous CELL/XMRK station markers did not fail closed");
        Require(result.stationCourseMarkerCount == 1,
            "exact station marker count included an ambiguous cell");
        Require(result.stationOrbitals.at(0x00000200).size() == 1 &&
                result.stationOrbitals.at(0x00000200).front().formID == 0x00000400,
            "exact CELL EDID to PNDT DNAM orbital join failed");
    }
}

int main()
{
    const auto nonce = std::chrono::high_resolution_clock::now()
                           .time_since_epoch().count();
    const auto fixtureRoot = std::filesystem::temp_directory_path() /
        ("CruiseFromStarmap-RecordReaderTests-" + std::to_string(nonce));
    try {
        std::filesystem::create_directories(fixtureRoot);
        RunRecordReaderTests(fixtureRoot);
        std::filesystem::remove_all(fixtureRoot);
        std::cout << "RecordReader tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(fixtureRoot, ignored);
        std::cerr << "RecordReader tests failed: " << error.what() << '\n';
        return 1;
    }
}
