#pragma once

#include "Bodies/SubrecordReader.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

enum class PluginMasterParseStatus : std::uint8_t
{
    Success,

    InvalidMasterName,
    MalformedSubrecordBody,
};

struct PluginMasterParseOutput
{
    PluginMasterParseStatus status {PluginMasterParseStatus::MalformedSubrecordBody};

    std::vector<std::string> masters;

    // Meaningful when status is MalformedSubrecordBody.
    SubrecordReadResult subrecordReadResult {SubrecordReadResult::End};
    std::size_t subrecordErrorOffset {0};

    bool Succeeded() const;
};

PluginMasterParseOutput ParsePluginMasters(std::span<const std::byte> decodedTes4Body);