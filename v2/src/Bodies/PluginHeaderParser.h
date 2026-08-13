#pragma once

#include "Bodies/PluginEntryReader.h"
#include "Bodies/PluginMasterParser.h"
#include "Bodies/RecordBodyDecoder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

enum class PluginHeaderParseStatus : std::uint8_t
{
    Success,

    MissingTes4,
    MalformedTes4Entry,
    FirstEntryIsGroup,
    UnexpectedFirstRecord,
    InvalidTes4FormId,

    Tes4BodyDecodeFailed,
    MasterParseFailed,
};

struct PluginHeaderParseOutput
{
    PluginHeaderParseStatus status {PluginHeaderParseStatus::MalformedTes4Entry};

    std::vector<std::string> masters;

    // Borrows the original plugin byte span and begins immediately after TES4.
    std::span<const std::byte> topLevelEntries;
    std::size_t topLevelOffset {0};

    // Relative to the beginning of pluginBytes.
    std::size_t failureOffset {0};

    // Meaningful for TES4 entry-envelope failures.
    PluginEntryReadResult entryReadResult {PluginEntryReadResult::End};

    // Meaningful when status is Tes4BodyDecodeFailed.
    RecordBodyDecodeStatus recordBodyDecodeStatus {RecordBodyDecodeStatus::Success};

    // Meaningful when status is MasterParseFailed.
    PluginMasterParseStatus masterParseStatus {PluginMasterParseStatus::Success};

    SubrecordReadResult subrecordReadResult {SubrecordReadResult::End};
    std::size_t subrecordErrorOffset {0};

    bool Succeeded() const;
};

// Parses only the leading TES4 record of one complete, already-loaded plugin.
// The returned topLevelEntries span remains valid only while pluginBytes remains alive and unmoved.
PluginHeaderParseOutput ParsePluginHeader(std::span<const std::byte> pluginBytes);