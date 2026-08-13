#pragma once

#include "Bodies/PlanetDataParser.h"
#include "Bodies/PluginEntryReader.h"
#include "Bodies/RecordBodyDecoder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

enum class PluginPlanetScanStatus : std::uint8_t
{
    Success,

    InvalidResolver,
    MalformedEntryContainer,
    RecordBodyDecodeFailed,
    PlanetDataParseFailed,
    DuplicateBodyId,
};

struct PluginPlanetScanOutput
{
    PluginPlanetScanStatus status {PluginPlanetScanStatus::MalformedEntryContainer};

    std::vector<IndexedBodyObservation> observations;

    std::vector<FormID> deletedBodyIds;

    std::size_t failureOffset {0};

    FormID pluginRecordFormId {0};

    PluginEntryReadResult entryReadResult {PluginEntryReadResult::End};

    RecordBodyDecodeStatus recordBodyDecodeStatus {RecordBodyDecodeStatus::Success};

    PlanetDataParseStatus planetDataParseStatus {PlanetDataParseStatus::Success};

    SubrecordReadResult subrecordReadResult {SubrecordReadResult::End};
    std::size_t subrecordErrorOffset {0};

    bool Succeeded() const;
};

// Scans one complete, already-loaded plugin byte span.
//
// The scanner traverses groups recursively, ignores unrelated records, decodes PNDT bodies, and returns plugin-local observations and deletion tombstones.
PluginPlanetScanOutput ScanPluginPlanets(std::span<const std::byte> pluginBytes, const PluginFormIdResolver& resolver);