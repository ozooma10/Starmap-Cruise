#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

inline constexpr std::size_t MaximumExpandedRecordBodySize = 64u * 1024u * 1024u;

enum class RecordBodyDecodeStatus : std::uint8_t
{
    Success,

    MissingExpandedSize,
    InvalidExpandedSize,
    ExpandedSizeTooLarge,
    MissingCompressedPayload,
    CompressedPayloadTooLarge,

    AllocationFailed,
    DecompressionFailed,
    ExpandedSizeMismatch,
    TrailingCompressedData,
};

struct RecordBodyDecodeOutput
{
    RecordBodyDecodeStatus status {RecordBodyDecodeStatus::DecompressionFailed};

    std::span<const std::byte> body;

    bool Succeeded() const;
};

RecordBodyDecodeOutput DecodeRecordBody(std::span<const std::byte> storedBody, bool compressed, std::vector<std::byte>& decompressionBuffer);