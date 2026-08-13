#include "Bodies/PluginHeaderParser.h"
#include "TestSuites.h"

#include <zlib.h>

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

    constexpr std::uint32_t CompressedRecordFlag = 0x00040000;

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

    void AppendBytes(Bytes& destination, const Bytes& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    void AppendSubrecord(Bytes& body, std::string_view signature, const Bytes& payload)
    {
        Require(payload.size() <= 0xFFFF, "test subrecord payload exceeded the 16-bit size field");

        AppendSignature(body, signature);
        AppendUInt16(body, static_cast<std::uint16_t>(payload.size()));
        AppendBytes(body, payload);
    }

    Bytes TextPayload(std::string_view text)
    {
        Bytes payload;
        payload.reserve(text.size() + 1);

        for (const char character : text) {
            payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }

        payload.push_back(std::byte {0});
        return payload;
    }

    Bytes CompressBody(const Bytes& expandedBody)
    {
        Require(!expandedBody.empty(), "compressed test body was empty");
        Require(expandedBody.size() <= 0xFFFFFFFF, "compressed test body exceeded the expanded-size field");

        const auto inputSize = static_cast<uLong>(expandedBody.size());
        uLongf compressedSize = ::compressBound(inputSize);
        std::vector<Bytef> compressed(compressedSize);

        Require(::compress2(compressed.data(), &compressedSize, reinterpret_cast<const Bytef*>(expandedBody.data()), inputSize, Z_BEST_SPEED) == Z_OK, "zlib header fixture compression failed");

        compressed.resize(compressedSize);

        Bytes storedBody;
        AppendUInt32(storedBody, static_cast<std::uint32_t>(expandedBody.size()));
        storedBody.insert(storedBody.end(), reinterpret_cast<const std::byte*>(compressed.data()), reinterpret_cast<const std::byte*>(compressed.data() + compressed.size()));
        return storedBody;
    }

    std::size_t AppendRecord(Bytes& plugin, std::string_view signature, std::uint32_t flags, ::FormID formId, const Bytes& body)
    {
        Require(body.size() <= 0xFFFFFFFF, "record test body exceeded the 32-bit size field");

        const auto offset = plugin.size();
        AppendSignature(plugin, signature);
        AppendUInt32(plugin, static_cast<std::uint32_t>(body.size()));
        AppendUInt32(plugin, flags);
        AppendUInt32(plugin, formId);
        AppendUInt32(plugin, 0);
        AppendUInt32(plugin, 0);
        AppendBytes(plugin, body);
        return offset;
    }

    void AppendEmptyGroup(Bytes& plugin, std::string_view label, std::uint32_t groupType)
    {
        Require(label.size() == 4, "test fixture used a non-four-byte group label");

        AppendSignature(plugin, "GRUP");
        AppendUInt32(plugin, static_cast<std::uint32_t>(::PluginEntryHeaderSize));
        AppendSignature(plugin, label);
        AppendUInt32(plugin, groupType);
        AppendUInt32(plugin, 0);
        AppendUInt32(plugin, 0);
    }

    void RequireSuccess(const ::PluginHeaderParseOutput& output, std::string_view message)
    {
        Require(output.Succeeded(), message);
        Require(output.status == ::PluginHeaderParseStatus::Success, "successful plugin header parse returned the wrong status");
        Require(output.entryReadResult == ::PluginEntryReadResult::Record, "successful plugin header parse lost the TES4 record result");
    }

    void RequireFailure(const ::PluginHeaderParseOutput& output, ::PluginHeaderParseStatus expectedStatus, std::string_view message)
    {
        Require(output.status == expectedStatus, message);
        Require(!output.Succeeded(), "failed plugin header parse reported success");
        Require(output.masters.empty(), "failed plugin header parse exposed partial master names");
        Require(output.topLevelEntries.empty(), "failed plugin header parse exposed top-level entries");
        Require(output.topLevelOffset == 0, "failed plugin header parse exposed a top-level offset");
    }

    void TestUncompressedHeaderOwnsMastersAndBorrowsExactRemainder()
    {
        Bytes tes4Body;
        AppendSubrecord(tes4Body, "HEDR", {std::byte {0x01}});
        AppendSubrecord(tes4Body, "MAST", TextPayload("Starfield.esm"));
        AppendSubrecord(tes4Body, "DATA", Bytes(8, std::byte {0}));
        AppendSubrecord(tes4Body, "MAST", TextPayload("ShatteredSpace.esm"));

        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, tes4Body);
        const auto expectedTopLevelOffset = plugin.size();

        const Bytes unvalidatedRemainder {
            std::byte {0xAA},
            std::byte {0xBB},
            std::byte {0xCC},
        };
        AppendBytes(plugin, unvalidatedRemainder);

        const auto output = ::ParsePluginHeader(plugin);

        RequireSuccess(output, "valid uncompressed TES4 header was rejected");
        Require(output.masters.size() == 2, "valid TES4 header returned the wrong master count");
        Require(output.masters[0] == "Starfield.esm" && output.masters[1] == "ShatteredSpace.esm", "TES4 master order or spelling changed");
        Require(output.topLevelOffset == expectedTopLevelOffset, "TES4 parser reported the wrong top-level offset");
        Require(output.topLevelEntries.data() == plugin.data() + expectedTopLevelOffset, "TES4 parser did not borrow the original remainder");
        Require(output.topLevelEntries.size() == unvalidatedRemainder.size(), "TES4 parser returned the wrong remainder size");
        Require(std::equal(output.topLevelEntries.begin(), output.topLevelEntries.end(), unvalidatedRemainder.begin()), "TES4 parser changed the remainder bytes");
    }

    void TestHeaderWithoutMastersOrRemainderSucceeds()
    {
        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, {});

        const auto output = ::ParsePluginHeader(plugin);

        RequireSuccess(output, "empty valid TES4 body was rejected");
        Require(output.masters.empty(), "empty TES4 body invented masters");
        Require(output.topLevelEntries.empty(), "header-only plugin invented top-level entries");
        Require(output.topLevelOffset == ::PluginEntryHeaderSize, "header-only plugin returned the wrong top-level offset");
    }

    void TestCompressedHeaderParsesMastersAndSlicesStoredRecordSize()
    {
        Bytes expandedBody;
        AppendSubrecord(expandedBody, "MAST", TextPayload("Starfield.esm"));

        const auto compressedBody = CompressBody(expandedBody);

        Bytes plugin;
        AppendRecord(plugin, "TES4", CompressedRecordFlag, 0, compressedBody);
        const auto expectedTopLevelOffset = plugin.size();
        AppendEmptyGroup(plugin, "PNDT", 0);

        const auto output = ::ParsePluginHeader(plugin);

        RequireSuccess(output, "valid compressed TES4 header was rejected");
        Require(output.masters.size() == 1 && output.masters[0] == "Starfield.esm", "compressed TES4 body returned the wrong master list");
        Require(output.topLevelOffset == expectedTopLevelOffset, "compressed TES4 used its expanded size for the remainder boundary");
        Require(output.topLevelEntries.data() == plugin.data() + expectedTopLevelOffset, "compressed TES4 remainder did not borrow original plugin storage");
        Require(output.topLevelEntries.size() == ::PluginEntryHeaderSize, "compressed TES4 returned the wrong top-level group size");
    }

    void TestEmptyPluginReportsMissingTes4()
    {
        const Bytes plugin;
        const auto output = ::ParsePluginHeader(plugin);

        RequireFailure(output, ::PluginHeaderParseStatus::MissingTes4, "empty plugin returned the wrong header status");
        Require(output.entryReadResult == ::PluginEntryReadResult::End, "empty plugin did not preserve the End result");
        Require(output.failureOffset == 0, "empty plugin invented a failure offset");
    }

    void TestMalformedTes4EnvelopePreservesEntryFailure()
    {
        const Bytes truncatedHeader(23, std::byte {0});
        const auto headerOutput = ::ParsePluginHeader(truncatedHeader);

        RequireFailure(headerOutput, ::PluginHeaderParseStatus::MalformedTes4Entry, "truncated TES4 header returned the wrong parser status");
        Require(headerOutput.entryReadResult == ::PluginEntryReadResult::TruncatedHeader, "truncated TES4 header lost the entry-reader result");
        Require(headerOutput.failureOffset == 0, "truncated TES4 header reported the wrong failure offset");

        Bytes truncatedBody;
        AppendSignature(truncatedBody, "TES4");
        AppendUInt32(truncatedBody, 1);
        AppendUInt32(truncatedBody, 0);
        AppendUInt32(truncatedBody, 0);
        AppendUInt32(truncatedBody, 0);
        AppendUInt32(truncatedBody, 0);

        const auto bodyOutput = ::ParsePluginHeader(truncatedBody);

        RequireFailure(bodyOutput, ::PluginHeaderParseStatus::MalformedTes4Entry, "truncated TES4 body returned the wrong parser status");
        Require(bodyOutput.entryReadResult == ::PluginEntryReadResult::TruncatedRecordBody, "truncated TES4 body lost the entry-reader result");
        Require(bodyOutput.failureOffset == 0, "truncated TES4 body reported the wrong failure offset");
    }

    void TestFirstGroupIsRejected()
    {
        Bytes plugin;
        AppendEmptyGroup(plugin, "PNDT", 0);

        const auto output = ::ParsePluginHeader(plugin);

        RequireFailure(output, ::PluginHeaderParseStatus::FirstEntryIsGroup, "leading group returned the wrong header status");
        Require(output.entryReadResult == ::PluginEntryReadResult::Group, "leading group did not preserve the Group result");
    }

    void TestUnexpectedFirstRecordIsRejected()
    {
        Bytes plugin;
        AppendRecord(plugin, "PNDT", 0, 0, {});

        const auto output = ::ParsePluginHeader(plugin);

        RequireFailure(output, ::PluginHeaderParseStatus::UnexpectedFirstRecord, "non-TES4 first record returned the wrong header status");
        Require(output.entryReadResult == ::PluginEntryReadResult::Record, "non-TES4 first record did not preserve the Record result");
    }

    void TestNonzeroTes4FormIdIsRejected()
    {
        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0x00000001, {});

        const auto output = ::ParsePluginHeader(plugin);

        RequireFailure(output, ::PluginHeaderParseStatus::InvalidTes4FormId, "nonzero TES4 FormID returned the wrong header status");
        Require(output.entryReadResult == ::PluginEntryReadResult::Record, "invalid TES4 FormID did not preserve the Record result");
    }

    void TestTes4DecodeFailureIsPreserved()
    {
        Bytes plugin;
        AppendRecord(plugin, "TES4", CompressedRecordFlag, 0, {std::byte {0x01}, std::byte {0x02}, std::byte {0x03}});

        const auto output = ::ParsePluginHeader(plugin);

        RequireFailure(output, ::PluginHeaderParseStatus::Tes4BodyDecodeFailed, "invalid compressed TES4 returned the wrong header status");
        Require(output.entryReadResult == ::PluginEntryReadResult::Record, "TES4 decode failure lost the Record result");
        Require(output.recordBodyDecodeStatus == ::RecordBodyDecodeStatus::MissingExpandedSize, "TES4 parser did not preserve the decoder failure");
    }

    void TestInvalidMasterFailureIsPreserved()
    {
        Bytes tes4Body;
        AppendSubrecord(tes4Body, "MAST", {});

        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, tes4Body);

        const auto output = ::ParsePluginHeader(plugin);

        RequireFailure(output, ::PluginHeaderParseStatus::MasterParseFailed, "invalid MAST returned the wrong header status");
        Require(output.masterParseStatus == ::PluginMasterParseStatus::InvalidMasterName, "TES4 parser did not preserve the invalid-master status");
        Require(output.subrecordReadResult == ::SubrecordReadResult::End, "invalid master invented a malformed subrecord result");
    }

    void TestMalformedMasterBodyProvenanceIsPreserved()
    {
        Bytes tes4Body;
        AppendSubrecord(tes4Body, "MAST", TextPayload("Starfield.esm"));
        const auto malformedOffset = tes4Body.size();
        tes4Body.insert(tes4Body.end(), {std::byte {0xAA}, std::byte {0xBB}, std::byte {0xCC}});

        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, tes4Body);

        const auto output = ::ParsePluginHeader(plugin);

        RequireFailure(output, ::PluginHeaderParseStatus::MasterParseFailed, "malformed TES4 subrecords returned the wrong header status");
        Require(output.masterParseStatus == ::PluginMasterParseStatus::MalformedSubrecordBody, "TES4 parser lost the malformed master-body status");
        Require(output.subrecordReadResult == ::SubrecordReadResult::TruncatedHeader, "TES4 parser lost the malformed subrecord-reader result");
        Require(output.subrecordErrorOffset == malformedOffset, "TES4 parser lost the decoded-TES4-body-relative error offset");
    }

    void RunTests()
    {
        TestUncompressedHeaderOwnsMastersAndBorrowsExactRemainder();
        TestHeaderWithoutMastersOrRemainderSucceeds();
        TestCompressedHeaderParsesMastersAndSlicesStoredRecordSize();
        TestEmptyPluginReportsMissingTes4();
        TestMalformedTes4EnvelopePreservesEntryFailure();
        TestFirstGroupIsRejected();
        TestUnexpectedFirstRecordIsRejected();
        TestNonzeroTes4FormIdIsRejected();
        TestTes4DecodeFailureIsPreserved();
        TestInvalidMasterFailureIsPreserved();
        TestMalformedMasterBodyProvenanceIsPreserved();
    }
}

void RunPluginHeaderParserTests()
{
    RunTests();
}
