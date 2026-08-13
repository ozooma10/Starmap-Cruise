#pragma once

#include "Domain/Destination.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

inline constexpr std::size_t PluginEntryHeaderSize = 24;

struct PluginRecordView
{
    std::array<char, 4> signature {};

    std::uint32_t flags {0};
    FormID formId {0};

    // Raw stored bytes. RecordBodyDecoder owns compressed versus
    // uncompressed decoding.
    std::span<const std::byte> storedBody;

    bool HasSignature(std::string_view expected) const;
};

struct PluginGroupView
{
    // Group labels may be four text bytes or a raw FormID-shaped label.
    std::array<std::byte, 4> label {};

    std::uint32_t groupType {0};

    // Immediate group contents, excluding the group's own 24-byte header.
    std::span<const std::byte> children;

    bool HasLabel(std::string_view expected) const;
};

using PluginEntryView = std::variant<std::monostate, PluginRecordView, PluginGroupView>;

enum class PluginEntryReadResult : std::uint8_t
{
    Record,
    Group,
    End,

    TruncatedHeader,
    TruncatedRecordBody,

    InvalidGroupSize,
    TruncatedGroup,
};

class PluginEntryReader
{
public:
    explicit PluginEntryReader(std::span<const std::byte> container);

    PluginEntryReadResult Next(PluginEntryView& entry);

    std::size_t Offset() const;
    std::size_t ErrorOffset() const;

private:
    PluginEntryReadResult Fail(PluginEntryReadResult result, std::size_t offset);

    std::span<const std::byte> container_;
    std::size_t offset_ {0};
    std::size_t errorOffset_ {0};
    std::optional<PluginEntryReadResult> terminal_;
};