#include "Bodies/PlanetDataParser.h"

namespace
{
    constexpr std::size_t MinimumGnamPayloadSize = 3 * sizeof(std::uint32_t);

    std::uint32_t ReadLittleEndianUInt32(std::span<const std::byte> bytes)
    {
        return std::to_integer<std::uint32_t>(bytes[0]) | (std::to_integer<std::uint32_t>(bytes[1]) << 8) | (std::to_integer<std::uint32_t>(bytes[2]) << 16) | (std::to_integer<std::uint32_t>(bytes[3]) << 24);
    }

    PlanetDataParseOutput Failure(PlanetDataParseStatus status)
    {
        return {
            .status = status,
        };
    }

    PlanetDataParseOutput MalformedBodyFailure(SubrecordReadResult readResult, std::size_t errorOffset)
    {
        return {
            .status = PlanetDataParseStatus::MalformedSubrecordBody,
            .subrecordReadResult = readResult,
            .subrecordErrorOffset = errorOffset,
        };
    }
}

bool PlanetDataParseOutput::Succeeded() const
{
    return status == PlanetDataParseStatus::Success && observation.has_value();
}

PlanetDataParseOutput ParsePlanetData(FormID pluginRecordFormId, std::span<const std::byte> decodedBody, const PluginFormIdResolver& resolver)
{
    if (!resolver.IsValid()) {
        return Failure(PlanetDataParseStatus::InvalidResolver);
    }

    if (pluginRecordFormId == 0) {
        return Failure(PlanetDataParseStatus::ZeroRecordFormId);
    }

    const auto recordId = resolver.Resolve(pluginRecordFormId);
    if (!recordId || *recordId == 0) {
        return Failure(PlanetDataParseStatus::UnresolvableRecordFormId);
    }

    SubrecordReader reader {decodedBody};
    SubrecordView subrecord;

    std::optional<FormID> systemId;

    while (true) {
        const auto readResult = reader.Next(subrecord);

        if (readResult == SubrecordReadResult::End) {
            break;
        }

        if (readResult != SubrecordReadResult::Record) {
            return MalformedBodyFailure(readResult, reader.ErrorOffset());
        }

        if (!subrecord.HasSignature("GNAM")) {
            continue;
        }

        if (systemId) {
            return Failure(PlanetDataParseStatus::DuplicateGnam);
        }

        if (subrecord.payload.size() < MinimumGnamPayloadSize) {
            return Failure(PlanetDataParseStatus::TruncatedGnam);
        }

        systemId = ReadLittleEndianUInt32(subrecord.payload.first(sizeof(std::uint32_t)));
    }

    if (!systemId) {
        return Failure(PlanetDataParseStatus::MissingGnam);
    }

    if (*systemId == 0) {
        return Failure(PlanetDataParseStatus::ZeroSystemId);
    }

    return {
        .status = PlanetDataParseStatus::Success,
        .observation = IndexedBodyObservation {
            .id = *recordId,
            .systemId = *systemId,
        },
    };
}