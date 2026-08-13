#include "Bodies/SubrecordReader.h"

#include <algorithm>
#include <cstring>

bool SubrecordView::HasSignature(std::string_view expected) const
{
    return expected.size() == signature.size() && std::equal( signature.begin(), signature.end(), expected.begin());
}

SubrecordReader::SubrecordReader(std::span<const std::byte> body) 
    : body_(body) {}

SubrecordReadResult SubrecordReader::Next(SubrecordView& subrecord)
{
    subrecord = {};

    if (terminal_) {
        return *terminal_;
    }

    while (true) {
        if (offset_ == body_.size()) {
            if (extendedSize_) {
                return Fail(SubrecordReadResult::DanglingExtendedSize, offset_);
            }

            terminal_ = SubrecordReadResult::End;
            return *terminal_;
        }

        const auto headerOffset = offset_;

        constexpr std::size_t headerSize = 6;

        if (body_.size() - offset_ < headerSize) {
            return Fail(SubrecordReadResult::TruncatedHeader, headerOffset);
        }

        std::array<char, 4> signature{};
        std::memcpy(signature.data(), body_.data() + offset_, signature.size());

        std::uint16_t size16 = 0;
        std::memcpy(&size16, body_.data() + offset_ + signature.size(), sizeof(size16));

        offset_ += headerSize;

        const bool isExtendedSize =
            std::memcmp(signature.data(), "XXXX", signature.size()) == 0;

        if (isExtendedSize) {
            if (extendedSize_) {
                return Fail(SubrecordReadResult::DuplicateExtendedSize, headerOffset);
            }

            if (size16 != sizeof(std::uint32_t)) {
                return Fail(SubrecordReadResult::InvalidExtendedSize, headerOffset);
            }

            if (body_.size() - offset_ < sizeof(std::uint32_t)) {
                return Fail(SubrecordReadResult::TruncatedPayload, headerOffset);
            }

            std::uint32_t extendedSize = 0;
            std::memcpy(&extendedSize, body_.data() + offset_, sizeof(extendedSize));

            offset_ += sizeof(extendedSize);
            extendedSize_ = extendedSize;
            continue;
        }

        const std::size_t payloadSize = extendedSize_ ? static_cast<std::size_t>(*extendedSize_) : static_cast<std::size_t>(size16);

        extendedSize_.reset();

        if (payloadSize > body_.size() - offset_) {
            return Fail(SubrecordReadResult::TruncatedPayload, headerOffset);
        }

        subrecord.signature = signature;
        subrecord.payload = body_.subspan(offset_, payloadSize);

        offset_ += payloadSize;

        return SubrecordReadResult::Record;
    }
}

std::size_t SubrecordReader::Offset() const
{
    return offset_;
}

std::size_t SubrecordReader::ErrorOffset() const
{
    return errorOffset_;
}

SubrecordReadResult SubrecordReader::Fail(SubrecordReadResult result, std::size_t offset)
{
    errorOffset_ = offset;
    terminal_ = result;
    return result;
}