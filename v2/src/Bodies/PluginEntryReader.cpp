#include "Bodies/PluginEntryReader.h"

#include <algorithm>
#include <cstring>

namespace
{
    std::uint32_t ReadLittleEndianUInt32(std::span<const std::byte> bytes)
    {
        return std::to_integer<std::uint32_t>(bytes[0]) | (std::to_integer<std::uint32_t>(bytes[1]) << 8) | (std::to_integer<std::uint32_t>(bytes[2]) << 16) | (std::to_integer<std::uint32_t>(bytes[3]) << 24);
    }
}

bool PluginRecordView::HasSignature(std::string_view expected) const
{
    return expected.size() == signature.size() && std::equal(signature.begin(), signature.end(), expected.begin());
}

bool PluginGroupView::HasLabel(std::string_view expected) const
{
    if (expected.size() != label.size()) {
        return false;
    }

    for (std::size_t index = 0; index < label.size(); ++index) {
        if (label[index] != static_cast<std::byte>(static_cast<unsigned char>(expected[index]))) {
            return false;
        }
    }

    return true;
}

PluginEntryReader::PluginEntryReader(std::span<const std::byte> container) : container_(container) {}

PluginEntryReadResult PluginEntryReader::Next(PluginEntryView& entry)
{
    entry = std::monostate {};

    if (terminal_) {
        return *terminal_;
    }

    if (offset_ == container_.size()) {
        terminal_ = PluginEntryReadResult::End;
        return *terminal_;
    }

    const auto headerOffset = offset_;

    if (container_.size() - offset_ < PluginEntryHeaderSize) {
        return Fail(PluginEntryReadResult::TruncatedHeader, headerOffset);
    }

    const auto header = container_.subspan(offset_, PluginEntryHeaderSize);

    std::array<char, 4> signature {};
    std::memcpy(signature.data(), header.data(), signature.size());

    const auto dataSize = ReadLittleEndianUInt32(header.subspan(4, sizeof(std::uint32_t)));
    const bool isGroup = std::memcmp(signature.data(), "GRUP", signature.size()) == 0;

    if (isGroup) {
        const auto groupSize = static_cast<std::size_t>(dataSize);

        if (groupSize < PluginEntryHeaderSize) {
            return Fail(PluginEntryReadResult::InvalidGroupSize, headerOffset);
        }

        if (groupSize > container_.size() - offset_) {
            return Fail(PluginEntryReadResult::TruncatedGroup, headerOffset);
        }

        std::array<std::byte, 4> label {};
        std::memcpy(label.data(), header.data() + 8, label.size());

        const auto groupType = ReadLittleEndianUInt32(header.subspan(12, sizeof(std::uint32_t)));

        entry = PluginGroupView {
            .label = label,
            .groupType = groupType,
            .children = container_.subspan(offset_ + PluginEntryHeaderSize, groupSize - PluginEntryHeaderSize),
        };

        offset_ += groupSize;

        return PluginEntryReadResult::Group;
    }

    const auto bodySize = static_cast<std::size_t>(dataSize);
    const auto availableBodyBytes = container_.size() - offset_ - PluginEntryHeaderSize;

    if (bodySize > availableBodyBytes) {
        return Fail(PluginEntryReadResult::TruncatedRecordBody, headerOffset);
    }

    const auto flags = ReadLittleEndianUInt32(header.subspan(8, sizeof(std::uint32_t)));
    const auto formId = ReadLittleEndianUInt32(header.subspan(12, sizeof(std::uint32_t)));

    entry = PluginRecordView {
        .signature = signature,
        .flags = flags,
        .formId = formId,
        .storedBody = container_.subspan(offset_ + PluginEntryHeaderSize, bodySize),
    };

    offset_ += PluginEntryHeaderSize + bodySize;

    return PluginEntryReadResult::Record;
}

std::size_t PluginEntryReader::Offset() const
{
    return offset_;
}

std::size_t PluginEntryReader::ErrorOffset() const
{
    return errorOffset_;
}

PluginEntryReadResult PluginEntryReader::Fail(PluginEntryReadResult result, std::size_t offset)
{
    errorOffset_ = offset;
    terminal_ = result;
    return result;
}