#include "Bodies/PluginHeaderParser.h"

#include <utility>
#include <variant>
#include <vector>

namespace
{
    constexpr std::uint32_t CompressedRecordFlag = 0x00040000;

    PluginHeaderParseOutput Failure(PluginHeaderParseStatus status)
    {
        return {
            .status = status,
        };
    }
}

bool PluginHeaderParseOutput::Succeeded() const
{
    return status == PluginHeaderParseStatus::Success;
}

PluginHeaderParseOutput ParsePluginHeader(std::span<const std::byte> pluginBytes)
{
    PluginEntryReader reader {pluginBytes};
    PluginEntryView entry;

    const auto readResult = reader.Next(entry);

    if (readResult == PluginEntryReadResult::End) {
        return Failure(PluginHeaderParseStatus::MissingTes4);
    }

    if (readResult != PluginEntryReadResult::Record && readResult != PluginEntryReadResult::Group) {
        auto output = Failure(PluginHeaderParseStatus::MalformedTes4Entry);

        output.failureOffset = reader.ErrorOffset();
        output.entryReadResult = readResult;
        return output;
    }

    if (readResult == PluginEntryReadResult::Group) {
        auto output = Failure(PluginHeaderParseStatus::FirstEntryIsGroup);

        output.entryReadResult = readResult;
        return output;
    }

    const auto record = std::get<PluginRecordView>(entry);

    if (!record.HasSignature("TES4")) {
        auto output = Failure(PluginHeaderParseStatus::UnexpectedFirstRecord);

        output.entryReadResult = readResult;
        return output;
    }

    if (record.formId != 0) {
        auto output = Failure(PluginHeaderParseStatus::InvalidTes4FormId);

        output.entryReadResult = readResult;
        return output;
    }

    const bool compressed = (record.flags & CompressedRecordFlag) != 0;

    std::vector<std::byte> decompressionBuffer;

    const auto decoded = DecodeRecordBody(record.storedBody, compressed, decompressionBuffer);

    if (!decoded.Succeeded()) {
        auto output = Failure(PluginHeaderParseStatus::Tes4BodyDecodeFailed);

        output.entryReadResult = readResult;
        output.recordBodyDecodeStatus = decoded.status;
        return output;
    }

    auto parsedMasters = ParsePluginMasters(decoded.body);

    if (!parsedMasters.Succeeded()) {
        auto output = Failure(PluginHeaderParseStatus::MasterParseFailed);

        output.entryReadResult = readResult;
        output.masterParseStatus = parsedMasters.status;
        output.subrecordReadResult = parsedMasters.subrecordReadResult;
        output.subrecordErrorOffset = parsedMasters.subrecordErrorOffset;

        return output;
    }

    return {
        .status = PluginHeaderParseStatus::Success,
        .masters = std::move(parsedMasters.masters),
        .topLevelEntries = pluginBytes.subspan(reader.Offset()),
        .topLevelOffset = reader.Offset(),
        .entryReadResult = readResult,
    };
}