#pragma once

#include "Bodies/PluginFormId.h"
#include "Bodies/SubrecordReader.h"
#include "Selection/SelectionPolicy.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

enum class PlanetDataParseStatus : std::uint8_t
{
    Success,

    InvalidResolver,
    ZeroRecordFormId,
    UnresolvableRecordFormId,

    MissingGnam,
    TruncatedGnam,
    DuplicateGnam,
    ZeroSystemId,

    MalformedSubrecordBody,
};

struct PlanetDataParseOutput
{
    PlanetDataParseStatus status {PlanetDataParseStatus::MalformedSubrecordBody};

    std::optional<IndexedBodyObservation> observation;

    // Meaningful when status is MalformedSubrecordBody.
    SubrecordReadResult subrecordReadResult {SubrecordReadResult::End};
    std::size_t subrecordErrorOffset {0};

    bool Succeeded() const;
};

PlanetDataParseOutput ParsePlanetData(FormID pluginRecordFormId, std::span<const std::byte> decodedBody, const PluginFormIdResolver& resolver);