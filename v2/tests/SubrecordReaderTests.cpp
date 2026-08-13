#include "Bodies/SubrecordReader.h"
#include "TestSuites.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Bytes = std::vector<std::byte>;

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error{ std::string{ message } };
    }

    void AppendSignature(Bytes& bytes, std::string_view signature)
    {
        Require(signature.size() == 4,
            "test fixture used a non-four-byte signature");

        for (const char character : signature) {
            bytes.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
    }

    void AppendUInt16(Bytes& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFF));
        bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    }

    void AppendUInt32(Bytes& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFF));
        bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
        bytes.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
        bytes.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
    }

    void AppendHeader(
        Bytes& bytes,
        std::string_view signature,
        std::uint16_t payloadSize)
    {
        AppendSignature(bytes, signature);
        AppendUInt16(bytes, payloadSize);
    }

    void AppendSubrecord(
        Bytes& bytes,
        std::string_view signature,
        const Bytes& payload)
    {
        Require(payload.size() <= 0xFFFF,
            "normal test subrecord payload exceeded 16-bit size");

        AppendHeader(
            bytes,
            signature,
            static_cast<std::uint16_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }

    void AppendExtendedSubrecord(
        Bytes& bytes,
        std::string_view signature,
        const Bytes& payload,
        std::uint16_t ignoredSize16 = 0)
    {
        Require(payload.size() <= 0xFFFFFFFF,
            "extended test subrecord payload exceeded 32-bit size");

        AppendHeader(bytes, "XXXX", sizeof(std::uint32_t));
        AppendUInt32(bytes, static_cast<std::uint32_t>(payload.size()));
        AppendHeader(bytes, signature, ignoredSize16);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }

    bool PayloadEquals(
        const ::SubrecordView& subrecord,
        const Bytes& expected)
    {
        return subrecord.payload.size() == expected.size() &&
               std::equal(
                   subrecord.payload.begin(),
                   subrecord.payload.end(),
                   expected.begin());
    }

    void TestReadsNormalSubrecordsInOrder()
    {
        const Bytes edid{
            std::byte{ 'A' },
            std::byte{ 'B' },
        };
        const Bytes gnam{
            std::byte{ 0x10 },
            std::byte{ 0x20 },
            std::byte{ 0x30 },
        };

        Bytes body;
        AppendSubrecord(body, "EDID", edid);
        AppendSubrecord(body, "GNAM", gnam);

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) == ::SubrecordReadResult::Record,
            "first normal subrecord was not returned");
        Require(subrecord.HasSignature("EDID"),
            "first normal subrecord retained the wrong signature");
        Require(!subrecord.HasSignature("edid"),
            "signature comparison ignored case");
        Require(!subrecord.HasSignature(std::string_view{ "EDID\0", 5 }),
            "signature comparison accepted the wrong length");
        Require(PayloadEquals(subrecord, edid),
            "first normal subrecord retained the wrong payload");
        Require(reader.Offset() == 8,
            "reader stopped at the wrong first-subrecord offset");

        Require(reader.Next(subrecord) == ::SubrecordReadResult::Record,
            "second normal subrecord was not returned");
        Require(subrecord.HasSignature("GNAM"),
            "second normal subrecord retained the wrong signature");
        Require(PayloadEquals(subrecord, gnam),
            "second normal subrecord retained the wrong payload");
        Require(reader.Offset() == body.size(),
            "reader did not consume the complete normal body");

        Require(reader.Next(subrecord) == ::SubrecordReadResult::End,
            "exact body exhaustion did not return End");
        Require(reader.Next(subrecord) == ::SubrecordReadResult::End,
            "End was not terminal");
    }

    void TestReadsZeroLengthNormalSubrecord()
    {
        Bytes body;
        AppendSubrecord(body, "EDID", {});

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) == ::SubrecordReadResult::Record,
            "zero-length normal subrecord was rejected");
        Require(subrecord.HasSignature("EDID"),
            "zero-length normal subrecord retained the wrong signature");
        Require(subrecord.payload.empty(),
            "zero-length normal subrecord gained a payload");
        Require(reader.Next(subrecord) == ::SubrecordReadResult::End,
            "zero-length normal subrecord did not end cleanly");
    }

    void TestExtendedSizeOverridesSixteenBitSize()
    {
        const Bytes payload(70'000, std::byte{ 0x5A });

        Bytes body;
        AppendExtendedSubrecord(body, "PNDT", payload, 1);

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) == ::SubrecordReadResult::Record,
            "extended-size subrecord was rejected");
        Require(subrecord.HasSignature("PNDT"),
            "extended-size subrecord retained the wrong signature");
        Require(subrecord.payload.size() == payload.size(),
            "extended size was replaced by the following 16-bit size");
        Require(subrecord.payload.front() == std::byte{ 0x5A } &&
                subrecord.payload.back() == std::byte{ 0x5A },
            "extended-size subrecord retained the wrong payload bounds");
        Require(reader.Offset() == body.size(),
            "extended-size subrecord stopped at the wrong offset");
        Require(reader.Next(subrecord) == ::SubrecordReadResult::End,
            "extended-size subrecord did not end cleanly");
    }

    void TestZeroExtendedSizeStillOverridesSize16()
    {
        Bytes body;
        AppendExtendedSubrecord(body, "EDID", {}, 9);

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) == ::SubrecordReadResult::Record,
            "zero extended size was treated as missing");
        Require(subrecord.HasSignature("EDID"),
            "zero-size extended subrecord retained the wrong signature");
        Require(subrecord.payload.empty(),
            "following 16-bit size overrode a present zero extended size");
        Require(reader.Next(subrecord) == ::SubrecordReadResult::End,
            "zero-size extended subrecord did not end cleanly");
    }

    void TestTruncatedHeaderFailsClosed()
    {
        Bytes body;
        AppendSubrecord(body, "EDID", { std::byte{ 0x01 } });
        body.push_back(std::byte{ 0xAA });
        body.push_back(std::byte{ 0xBB });
        body.push_back(std::byte{ 0xCC });

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) == ::SubrecordReadResult::Record,
            "valid subrecord before truncated header was rejected");
        const auto errorOffset = reader.Offset();

        Require(reader.Next(subrecord) ==
                    ::SubrecordReadResult::TruncatedHeader,
            "trailing partial header was accepted");
        Require(reader.ErrorOffset() == errorOffset,
            "truncated-header error offset was incorrect");
        Require(subrecord.payload.empty(),
            "failed read exposed the previous payload");
    }

    void TestTruncatedPayloadFailureIsTerminal()
    {
        Bytes body;
        AppendHeader(body, "GNAM", 4);
        body.push_back(std::byte{ 0x01 });
        body.push_back(std::byte{ 0x02 });

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) ==
                    ::SubrecordReadResult::TruncatedPayload,
            "truncated normal payload was accepted");
        Require(reader.ErrorOffset() == 0,
            "truncated-payload error did not identify its header");

        const auto stoppedOffset = reader.Offset();
        Require(reader.Next(subrecord) ==
                    ::SubrecordReadResult::TruncatedPayload,
            "reader resumed after a truncated payload");
        Require(reader.Offset() == stoppedOffset,
            "terminal payload failure advanced the reader");
    }

    void TestMalformedExtendedSizeHeaderFails()
    {
        Bytes body;
        AppendHeader(body, "XXXX", 3);
        body.insert(body.end(), 3, std::byte{ 0x00 });

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) ==
                    ::SubrecordReadResult::InvalidExtendedSize,
            "XXXX header with a non-four-byte payload was accepted");
        Require(reader.ErrorOffset() == 0,
            "invalid XXXX size reported the wrong header offset");
    }

    void TestTruncatedExtendedSizePayloadFails()
    {
        Bytes body;
        AppendHeader(body, "XXXX", sizeof(std::uint32_t));
        AppendUInt16(body, 12);

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) ==
                    ::SubrecordReadResult::TruncatedPayload,
            "truncated XXXX payload was accepted");
        Require(reader.ErrorOffset() == 0,
            "truncated XXXX payload reported the wrong offset");
    }

    void TestDuplicateExtendedSizeFails()
    {
        Bytes body;
        AppendHeader(body, "XXXX", sizeof(std::uint32_t));
        AppendUInt32(body, 2);
        AppendHeader(body, "XXXX", sizeof(std::uint32_t));
        AppendUInt32(body, 2);

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) ==
                    ::SubrecordReadResult::DuplicateExtendedSize,
            "consecutive XXXX prefixes were accepted");
        Require(reader.ErrorOffset() == 10,
            "duplicate XXXX did not identify its own header");
    }

    void TestDanglingExtendedSizeFails()
    {
        Bytes body;
        AppendHeader(body, "XXXX", sizeof(std::uint32_t));
        AppendUInt32(body, 8);

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) ==
                    ::SubrecordReadResult::DanglingExtendedSize,
            "XXXX prefix without a following subrecord was accepted");
        Require(reader.ErrorOffset() == body.size(),
            "dangling XXXX did not identify the missing-subrecord offset");
    }

    void TestTruncatedExtendedSubrecordFailsAtRealHeader()
    {
        Bytes body;
        AppendHeader(body, "XXXX", sizeof(std::uint32_t));
        AppendUInt32(body, 5);
        AppendHeader(body, "PNDT", 0);
        body.push_back(std::byte{ 0x01 });
        body.push_back(std::byte{ 0x02 });

        ::SubrecordReader reader{ body };
        ::SubrecordView subrecord;

        Require(reader.Next(subrecord) ==
                    ::SubrecordReadResult::TruncatedPayload,
            "truncated extended subrecord payload was accepted");
        Require(reader.ErrorOffset() == 10,
            "extended payload failure did not identify the real header");
    }

    void RunTests()
    {
        TestReadsNormalSubrecordsInOrder();
        TestReadsZeroLengthNormalSubrecord();
        TestExtendedSizeOverridesSixteenBitSize();
        TestZeroExtendedSizeStillOverridesSize16();
        TestTruncatedHeaderFailsClosed();
        TestTruncatedPayloadFailureIsTerminal();
        TestMalformedExtendedSizeHeaderFails();
        TestTruncatedExtendedSizePayloadFails();
        TestDuplicateExtendedSizeFails();
        TestDanglingExtendedSizeFails();
        TestTruncatedExtendedSubrecordFailsAtRealHeader();
    }
}

void RunSubrecordReaderTests()
{
    RunTests();
}
