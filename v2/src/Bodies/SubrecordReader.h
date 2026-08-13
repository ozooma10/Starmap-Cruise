#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

struct SubrecordView
{
    std::array<char, 4> signature {};
    std::span<const std::byte> payload;

    bool HasSignature(std::string_view expected) const;
};

enum class SubrecordReadResult : std::uint8_t
{
    Record,
    End,

    TruncatedHeader,
    TruncatedPayload,

    InvalidExtendedSize,
    DuplicateExtendedSize,
    DanglingExtendedSize,
};

class SubrecordReader
{
public:
    explicit SubrecordReader(std::span<const std::byte> body);

    SubrecordReadResult Next(SubrecordView& subrecord);

    std::size_t Offset() const;
    std::size_t ErrorOffset() const;

private:
    SubrecordReadResult Fail(SubrecordReadResult result, std::size_t offset);

    std::span<const std::byte> body_;
    std::size_t offset_ {0};
    std::size_t errorOffset_ {0};
    std::optional<std::uint32_t> extendedSize_;
    std::optional<SubrecordReadResult> terminal_;
};