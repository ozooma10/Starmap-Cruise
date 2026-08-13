#include "Bodies/PluginMasterParser.h"

#include <algorithm>
#include <utility>

namespace
{
    bool IsValidMasterName(std::span<const std::byte> payload)
    {
        // A MAST name must contain at least one character followed by one terminating null byte.
        if (payload.size() < 2 || payload.back() != std::byte {0}) {
            return false;
        }

        const auto terminator = payload.end() - 1;

        // Embedded nulls would make the stored owner name ambiguous.
        return std::find(payload.begin(), terminator, std::byte {0}) == terminator;
    }

    PluginMasterParseOutput Failure(PluginMasterParseStatus status)
    {
        return {
            .status = status,
        };
    }

    PluginMasterParseOutput MalformedBodyFailure(SubrecordReadResult readResult, std::size_t errorOffset)
    {
        return {
            .status = PluginMasterParseStatus::MalformedSubrecordBody,
            .subrecordReadResult = readResult,
            .subrecordErrorOffset = errorOffset,
        };
    }
}

bool PluginMasterParseOutput::Succeeded() const
{
    return status == PluginMasterParseStatus::Success;
}

PluginMasterParseOutput ParsePluginMasters(std::span<const std::byte> decodedTes4Body)
{
    SubrecordReader reader {decodedTes4Body};
    SubrecordView subrecord;

    std::vector<std::string> masters;

    while (true) {
        const auto readResult = reader.Next(subrecord);

        if (readResult == SubrecordReadResult::End) {
            break;
        }

        if (readResult != SubrecordReadResult::Record) {
            return MalformedBodyFailure(readResult, reader.ErrorOffset());
        }

        if (!subrecord.HasSignature("MAST")) {
            continue;
        }

        if (!IsValidMasterName(subrecord.payload)) {
            return Failure(PluginMasterParseStatus::InvalidMasterName);
        }

        masters.emplace_back(reinterpret_cast<const char*>(subrecord.payload.data()), subrecord.payload.size() - 1);
    }

    return {
        .status = PluginMasterParseStatus::Success,
        .masters = std::move(masters),
    };
}