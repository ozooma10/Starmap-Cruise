#include "Bodies/PluginMasterParser.h"
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

    Bytes TextPayload(std::string_view text, bool terminate = true)
    {
        Bytes payload;
        payload.reserve(text.size() + static_cast<std::size_t>(terminate));

        for (const char character : text) {
            payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }

        if (terminate) {
            payload.push_back(std::byte {0});
        }

        return payload;
    }

    void RequireFailure(const ::PluginMasterParseOutput& output, ::PluginMasterParseStatus expectedStatus, std::string_view message)
    {
        Require(output.status == expectedStatus, message);
        Require(!output.Succeeded(), "failed TES4 master parse reported success");
        Require(output.masters.empty(), "failed TES4 master parse exposed a partial master list");
    }

    void TestEmptyBodyHasNoMasters()
    {
        const Bytes body;
        const auto output = ::ParsePluginMasters(body);

        Require(output.Succeeded(), "empty TES4 body was rejected");
        Require(output.status == ::PluginMasterParseStatus::Success, "empty TES4 body returned the wrong status");
        Require(output.masters.empty(), "empty TES4 body invented a master");
    }

    void TestSingleMasterRemovesTerminator()
    {
        Bytes body;
        AppendSubrecord(body, "MAST", TextPayload("Starfield.esm"));

        const auto output = ::ParsePluginMasters(body);

        Require(output.Succeeded(), "valid single-master TES4 body was rejected");
        Require(output.masters.size() == 1, "single MAST did not produce exactly one master");
        Require(output.masters[0] == "Starfield.esm", "single MAST retained the terminator or changed the name");
    }

    void TestMultipleMastersPreserveOrderAndIgnoreUnknownFields()
    {
        Bytes body;
        AppendSubrecord(body, "HEDR", {std::byte {0x01}, std::byte {0x02}});
        AppendSubrecord(body, "MAST", TextPayload("Starfield.esm"));
        AppendSubrecord(body, "DATA", Bytes(8, std::byte {0}));
        AppendSubrecord(body, "MAST", TextPayload("ShatteredSpace.esm"));
        AppendSubrecord(body, "ONAM", {std::byte {0xAA}});

        const auto output = ::ParsePluginMasters(body);

        Require(output.Succeeded(), "valid ordered TES4 master list was rejected");
        Require(output.masters.size() == 2, "ordered TES4 body returned the wrong master count");
        Require(output.masters[0] == "Starfield.esm", "first MAST changed position or spelling");
        Require(output.masters[1] == "ShatteredSpace.esm", "second MAST changed position or spelling");
    }

    void TestDuplicateAndCaseVariantNamesArePreserved()
    {
        Bytes body;
        AppendSubrecord(body, "MAST", TextPayload("Starfield.esm"));
        AppendSubrecord(body, "MAST", TextPayload("STARFIELD.ESM"));
        AppendSubrecord(body, "MAST", TextPayload("Starfield.esm"));

        const auto output = ::ParsePluginMasters(body);

        Require(output.Succeeded(), "syntactically valid duplicate MAST names were rejected by the syntax parser");
        Require(output.masters.size() == 3, "duplicate MAST names were collapsed");
        Require(output.masters[0] == "Starfield.esm", "first duplicate MAST changed");
        Require(output.masters[1] == "STARFIELD.ESM", "case-variant MAST was normalized early");
        Require(output.masters[2] == "Starfield.esm", "last duplicate MAST changed");
    }

    void TestExtendedSizeMasterUsesExtendedPayloadSize()
    {
        Bytes body;
        AppendExtendedSubrecord(body, "MAST", TextPayload("Starfield.esm"), 1);

        const auto output = ::ParsePluginMasters(body);

        Require(output.Succeeded(), "XXXX-sized MAST was rejected");
        Require(output.masters.size() == 1 && output.masters[0] == "Starfield.esm", "XXXX-sized MAST used the ignored 16-bit size");
    }

    void TestEmptyMasterPayloadFails()
    {
        Bytes body;
        AppendSubrecord(body, "MAST", {});

        const auto output = ::ParsePluginMasters(body);

        RequireFailure(output, ::PluginMasterParseStatus::InvalidMasterName, "empty MAST payload returned the wrong status");
    }

    void TestTerminatorOnlyMasterFails()
    {
        Bytes body;
        AppendSubrecord(body, "MAST", {std::byte {0}});

        const auto output = ::ParsePluginMasters(body);

        RequireFailure(output, ::PluginMasterParseStatus::InvalidMasterName, "terminator-only MAST returned the wrong status");
    }

    void TestUnterminatedMasterFails()
    {
        Bytes body;
        AppendSubrecord(body, "MAST", TextPayload("Starfield.esm", false));

        const auto output = ::ParsePluginMasters(body);

        RequireFailure(output, ::PluginMasterParseStatus::InvalidMasterName, "unterminated MAST returned the wrong status");
    }

    void TestEmbeddedNullDiscardsEarlierMasters()
    {
        Bytes body;
        AppendSubrecord(body, "MAST", TextPayload("Starfield.esm"));
        AppendSubrecord(
            body,
            "MAST",
            {
                std::byte {'B'},
                std::byte {0},
                std::byte {'a'},
                std::byte {'d'},
                std::byte {0},
            }
        );

        const auto output = ::ParsePluginMasters(body);

        RequireFailure(output, ::PluginMasterParseStatus::InvalidMasterName, "embedded-null MAST returned the wrong status");
    }

    void TestMalformedBodyAfterMasterFails()
    {
        Bytes body;
        AppendSubrecord(body, "MAST", TextPayload("Starfield.esm"));
        const auto malformedOffset = body.size();
        body.push_back(std::byte {0xAA});
        body.push_back(std::byte {0xBB});
        body.push_back(std::byte {0xCC});

        const auto output = ::ParsePluginMasters(body);

        RequireFailure(output, ::PluginMasterParseStatus::MalformedSubrecordBody, "malformed bytes after MAST were accepted");
        Require(output.subrecordReadResult == ::SubrecordReadResult::TruncatedHeader, "parser did not preserve the malformed reader result");
        Require(output.subrecordErrorOffset == malformedOffset, "parser did not preserve the malformed reader offset");
    }

    void RunTests()
    {
        TestEmptyBodyHasNoMasters();
        TestSingleMasterRemovesTerminator();
        TestMultipleMastersPreserveOrderAndIgnoreUnknownFields();
        TestDuplicateAndCaseVariantNamesArePreserved();
        TestExtendedSizeMasterUsesExtendedPayloadSize();
        TestEmptyMasterPayloadFails();
        TestTerminatorOnlyMasterFails();
        TestUnterminatedMasterFails();
        TestEmbeddedNullDiscardsEarlierMasters();
        TestMalformedBodyAfterMasterFails();
    }
}

void RunPluginMasterParserTests()
{
    RunTests();
}
