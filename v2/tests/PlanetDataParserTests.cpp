#include "Bodies/PlanetDataParser.h"
#include "TestSuites.h"

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
            throw std::runtime_error {std::string {message}};
    }

    void AppendSignature(Bytes& bytes, std::string_view signature)
    {
        Require(signature.size() == 4, "test fixture used a non-four-byte signature");

        for (const char character : signature) {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
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

    void AppendHeader(Bytes& bytes, std::string_view signature, std::uint16_t payloadSize)
    {
        AppendSignature(bytes, signature);
        AppendUInt16(bytes, payloadSize);
    }

    void AppendSubrecord(Bytes& bytes, std::string_view signature, const Bytes& payload)
    {
        Require(payload.size() <= 0xFFFF, "normal test subrecord payload exceeded 16-bit size");

        AppendHeader(bytes, signature, static_cast<std::uint16_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }

    void AppendExtendedSubrecord(Bytes& bytes, std::string_view signature, const Bytes& payload, std::uint16_t ignoredSize16 = 0)
    {
        Require(payload.size() <= 0xFFFFFFFF, "extended test subrecord payload exceeded 32-bit size");

        AppendHeader(bytes, "XXXX", sizeof(std::uint32_t));
        AppendUInt32(bytes, static_cast<std::uint32_t>(payload.size()));
        AppendHeader(bytes, signature, ignoredSize16);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }

    Bytes GnamPayload(std::uint32_t systemId, std::uint32_t parentId = 2, std::uint32_t planetId = 3)
    {
        Bytes payload;
        AppendUInt32(payload, systemId);
        AppendUInt32(payload, parentId);
        AppendUInt32(payload, planetId);
        return payload;
    }

    Bytes PlanetBody(std::uint32_t systemId)
    {
        Bytes body;
        AppendSubrecord(body, "GNAM", GnamPayload(systemId));
        return body;
    }

    ::PluginFormIdResolver FullSelfResolver()
    {
        return {
            {},
            {
                .tier = ::PluginTier::Full,
                .index = 5,
            },
        };
    }

    void RequireFailure(const ::PlanetDataParseOutput& output, ::PlanetDataParseStatus expectedStatus, std::string_view message)
    {
        Require(output.status == expectedStatus, message);
        Require(!output.Succeeded(), "failed PNDT parse reported success");
        Require(!output.observation.has_value(), "failed PNDT parse exposed an observation");
    }

    void TestValidSelfOwnedPlanetData()
    {
        const auto resolver = FullSelfResolver();
        const auto gnam = GnamPayload(0x11223344, 0x55667788, 0x99AABBCC);
        Require(gnam.size() == 12, "exact-size GNAM fixture was not twelve bytes");

        Bytes body;
        AppendSubrecord(body, "GNAM", gnam);

        const auto output = ::ParsePlanetData(0x00000400, body, resolver);

        Require(output.Succeeded(), "valid self-owned PNDT was rejected");
        Require(output.status == ::PlanetDataParseStatus::Success, "valid self-owned PNDT returned the wrong status");
        Require(output.observation.has_value(), "valid self-owned PNDT returned no observation");
        Require(output.observation->id == 0x05000400, "self-owned PNDT resolved to the wrong runtime FormID");
        Require(output.observation->systemId == 0x11223344, "PNDT parser did not read the first little-endian GNAM value");
    }

    void TestMasterOverrideRecordResolution()
    {
        const ::PluginFormIdResolver resolver {
            {
                {
                    .tier = ::PluginTier::Full,
                    .index = 2,
                },
            },
            {
                .tier = ::PluginTier::Full,
                .index = 5,
            },
        };

        const auto output = ::ParsePlanetData(0x00000410, PlanetBody(7), resolver);

        Require(output.Succeeded(), "valid master override PNDT was rejected");
        Require(output.observation->id == 0x02000410, "master override PNDT was reinterpreted as self-owned");
        Require(output.observation->systemId == 7, "master override PNDT retained the wrong system ID");
    }

    void TestSmallPluginSelfRecordResolution()
    {
        const ::PluginFormIdResolver resolver {
            {},
            {
                .tier = ::PluginTier::Small,
                .index = 2,
            },
        };

        const auto output = ::ParsePlanetData(0x00000678, PlanetBody(9), resolver);

        Require(output.Succeeded(), "valid small-plugin PNDT was rejected");
        Require(output.observation->id == 0xFE002678, "small-plugin PNDT resolved to the wrong compact FormID");
        Require(output.observation->systemId == 9, "small-plugin PNDT retained the wrong system ID");
    }

    void TestUnknownSubrecordsAreIgnored()
    {
        const auto resolver = FullSelfResolver();

        Bytes body;
        AppendSubrecord(body, "EDID", {std::byte {'A'}, std::byte {'B'}});
        AppendSubrecord(body, "DNAM", {std::byte {0x10}, std::byte {0x20}});
        AppendSubrecord(body, "GNAM", GnamPayload(0x1234));
        AppendSubrecord(body, "ABCD", {});

        const auto output = ::ParsePlanetData(0x00000400, body, resolver);

        Require(output.Succeeded(), "unknown PNDT subrecords caused the parser to fail");
        Require(output.observation->systemId == 0x1234, "unknown subrecords changed the parsed system ID");
    }

    void TestGnamTrailingBytesArePermitted()
    {
        const auto resolver = FullSelfResolver();
        auto gnam = GnamPayload(0x10203040);
        gnam.push_back(std::byte {0xAA});
        gnam.push_back(std::byte {0xBB});

        Bytes body;
        AppendSubrecord(body, "GNAM", gnam);

        const auto output = ::ParsePlanetData(0x00000400, body, resolver);

        Require(output.Succeeded(), "GNAM trailing bytes were rejected");
        Require(output.observation->systemId == 0x10203040, "GNAM trailing bytes changed the parsed system ID");
    }

    void TestExtendedSizeGnamUsesExtendedPayloadSize()
    {
        const auto resolver = FullSelfResolver();

        Bytes body;
        AppendExtendedSubrecord(body, "GNAM", GnamPayload(0x01020304), 1);

        const auto output = ::ParsePlanetData(0x00000400, body, resolver);

        Require(output.Succeeded(), "XXXX-sized GNAM was rejected");
        Require(output.observation->systemId == 0x01020304, "XXXX-sized GNAM used the ignored 16-bit size");
    }

    void TestMissingGnamFails()
    {
        const auto resolver = FullSelfResolver();

        Bytes body;
        AppendSubrecord(body, "EDID", {std::byte {'A'}});

        const auto output = ::ParsePlanetData(0x00000400, body, resolver);

        RequireFailure(output, ::PlanetDataParseStatus::MissingGnam, "PNDT without GNAM returned the wrong status");
    }

    void TestTruncatedGnamFails()
    {
        const auto resolver = FullSelfResolver();
        const Bytes truncatedGnam(11, std::byte {0x01});

        Bytes body;
        AppendSubrecord(body, "GNAM", truncatedGnam);

        const auto output = ::ParsePlanetData(0x00000400, body, resolver);

        RequireFailure(output, ::PlanetDataParseStatus::TruncatedGnam, "short GNAM returned the wrong status");
    }

    void TestDuplicateGnamFails()
    {
        const auto resolver = FullSelfResolver();

        Bytes body;
        AppendSubrecord(body, "GNAM", GnamPayload(7));
        AppendSubrecord(body, "GNAM", GnamPayload(8));

        const auto output = ::ParsePlanetData(0x00000400, body, resolver);

        RequireFailure(output, ::PlanetDataParseStatus::DuplicateGnam, "duplicate GNAM returned the wrong status");
    }

    void TestZeroSystemIdFails()
    {
        const auto resolver = FullSelfResolver();
        const auto output = ::ParsePlanetData(0x00000400, PlanetBody(0), resolver);

        RequireFailure(output, ::PlanetDataParseStatus::ZeroSystemId, "zero GNAM system ID returned the wrong status");
    }

    void TestMalformedBodyAfterGnamFails()
    {
        const auto resolver = FullSelfResolver();
        auto body = PlanetBody(7);
        const auto malformedOffset = body.size();
        body.push_back(std::byte {0xAA});
        body.push_back(std::byte {0xBB});
        body.push_back(std::byte {0xCC});

        const auto output = ::ParsePlanetData(0x00000400, body, resolver);

        RequireFailure(output, ::PlanetDataParseStatus::MalformedSubrecordBody, "malformed bytes after GNAM were accepted");
        Require(output.subrecordReadResult == ::SubrecordReadResult::TruncatedHeader, "parser did not preserve the malformed reader result");
        Require(output.subrecordErrorOffset == malformedOffset, "parser did not preserve the malformed reader offset");
    }

    void TestInvalidResolverFailsBeforeParsing()
    {
        const ::PluginFormIdResolver resolver {
            {},
            {
                .tier = ::PluginTier::Small,
                .index = 0x1000,
            },
        };

        const auto output = ::ParsePlanetData(0x00000400, PlanetBody(7), resolver);

        RequireFailure(output, ::PlanetDataParseStatus::InvalidResolver, "invalid resolver returned the wrong status");
    }

    void TestZeroRecordFormIdFails()
    {
        const auto resolver = FullSelfResolver();
        const auto output = ::ParsePlanetData(0, PlanetBody(7), resolver);

        RequireFailure(output, ::PlanetDataParseStatus::ZeroRecordFormId, "zero PNDT FormID returned the wrong status");
    }

    void TestUnknownOwnerSlotFails()
    {
        const ::PluginFormIdResolver resolver {
            {
                {
                    .tier = ::PluginTier::Full,
                    .index = 0,
                },
            },
            {
                .tier = ::PluginTier::Full,
                .index = 5,
            },
        };

        const auto output = ::ParsePlanetData(0x02000400, PlanetBody(7), resolver);

        RequireFailure(output, ::PlanetDataParseStatus::UnresolvableRecordFormId, "unknown PNDT owner slot returned the wrong status");
    }

    void RunTests()
    {
        TestValidSelfOwnedPlanetData();
        TestMasterOverrideRecordResolution();
        TestSmallPluginSelfRecordResolution();
        TestUnknownSubrecordsAreIgnored();
        TestGnamTrailingBytesArePermitted();
        TestExtendedSizeGnamUsesExtendedPayloadSize();
        TestMissingGnamFails();
        TestTruncatedGnamFails();
        TestDuplicateGnamFails();
        TestZeroSystemIdFails();
        TestMalformedBodyAfterGnamFails();
        TestInvalidResolverFailsBeforeParsing();
        TestZeroRecordFormIdFails();
        TestUnknownOwnerSlotFails();
    }
}

void RunPlanetDataParserTests()
{
    RunTests();
}
