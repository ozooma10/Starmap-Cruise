#include "Bodies/RecordBodyDecoder.h"

#include <zlib.h>

#include <limits>
#include <new>
#include <stdexcept>

namespace
{
    std::uint32_t ReadUInt32(std::span<const std::byte> bytes)
    {
        return std::to_integer<std::uint32_t>(bytes[0]) | (std::to_integer<std::uint32_t>(bytes[1]) << 8) | (std::to_integer<std::uint32_t>(bytes[2]) << 16) | (std::to_integer<std::uint32_t>(bytes[3]) << 24);
    }

    RecordBodyDecodeOutput Failure(RecordBodyDecodeStatus status)
    {
        return {
            .status = status,
            .body = {},
        };
    }
}

bool RecordBodyDecodeOutput::Succeeded() const
{
    return status == RecordBodyDecodeStatus::Success;
}

RecordBodyDecodeOutput DecodeRecordBody(std::span<const std::byte> storedBody, bool compressed, std::vector<std::byte>& decompressionBuffer)
{
    decompressionBuffer.clear();

    if (!compressed) {
        return {
            .status = RecordBodyDecodeStatus::Success,
            .body = storedBody,
        };
    }

    constexpr std::size_t expandedSizeFieldSize = sizeof(std::uint32_t);

    if (storedBody.size() < expandedSizeFieldSize) {
        return Failure(RecordBodyDecodeStatus::MissingExpandedSize);
    }

    const auto expandedSize = ReadUInt32(storedBody.first(expandedSizeFieldSize));
    if (expandedSize == 0) {
        return Failure(RecordBodyDecodeStatus::InvalidExpandedSize);
    }

    if (expandedSize > MaximumExpandedRecordBodySize) {
        return Failure(RecordBodyDecodeStatus::ExpandedSizeTooLarge);
    }

    const auto compressedBody = storedBody.subspan(expandedSizeFieldSize);

    if (compressedBody.empty()) {
        return Failure(RecordBodyDecodeStatus::MissingCompressedPayload);
    }

    if (compressedBody.size() > std::numeric_limits<uLong>::max()) {
        return Failure(RecordBodyDecodeStatus::CompressedPayloadTooLarge);
    }

    try {
        decompressionBuffer.resize(expandedSize);
    } catch (const std::bad_alloc&) {
        decompressionBuffer.clear();
        return Failure(RecordBodyDecodeStatus::AllocationFailed);
    } catch (const std::length_error&) {
        decompressionBuffer.clear();
        return Failure(RecordBodyDecodeStatus::AllocationFailed);
    }

    uLongf producedSize = static_cast<uLongf>(decompressionBuffer.size());

    uLong consumedSize = static_cast<uLong>(compressedBody.size());

    const auto result = ::uncompress2(reinterpret_cast<Bytef*>(decompressionBuffer.data()), &producedSize, reinterpret_cast<const Bytef*>(compressedBody.data()), &consumedSize);

    if (result != Z_OK) {
        decompressionBuffer.clear();
        return Failure(RecordBodyDecodeStatus::DecompressionFailed);
    }

    if (producedSize != expandedSize) {
        decompressionBuffer.clear();
        return Failure(RecordBodyDecodeStatus::ExpandedSizeMismatch);
    }

    if (consumedSize != compressedBody.size()) {
        decompressionBuffer.clear();
        return Failure(RecordBodyDecodeStatus::TrailingCompressedData);
    }

    return {
        .status = RecordBodyDecodeStatus::Success,
        .body = {
            decompressionBuffer.data(),
            decompressionBuffer.size(),
        },
    };
}
