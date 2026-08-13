#include "Bodies/PluginPlanetScanner.h"
#include "TestSuites.h"

#include <zlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Bytes = std::vector<std::byte>;

    constexpr std::uint32_t DeletedRecordFlag = 0x00000020;
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

    Bytes PlanetBody(::FormID systemId)
    {
        Bytes payload;
        AppendUInt32(payload, systemId);
        AppendUInt32(payload, 0x11111111);
        AppendUInt32(payload, 0x22222222);

        Bytes body;
        AppendSubrecord(body, "GNAM", payload);
        return body;
    }

    Bytes CompressBody(const Bytes& expandedBody)
    {
        Require(!expandedBody.empty(), "compressed test body was empty");
        Require(expandedBody.size() <= 0xFFFFFFFF, "compressed test body exceeded the expanded-size field");

        const auto inputSize = static_cast<uLong>(expandedBody.size());
        uLongf compressedSize = ::compressBound(inputSize);
        std::vector<Bytef> compressed(compressedSize);

        Require(::compress2(compressed.data(), &compressedSize, reinterpret_cast<const Bytef*>(expandedBody.data()), inputSize, Z_BEST_SPEED) == Z_OK, "zlib scanner fixture compression failed");

        compressed.resize(compressedSize);

        Bytes storedBody;
        AppendUInt32(storedBody, static_cast<std::uint32_t>(expandedBody.size()));
        storedBody.insert(storedBody.end(), reinterpret_cast<const std::byte*>(compressed.data()), reinterpret_cast<const std::byte*>(compressed.data() + compressed.size()));
        return storedBody;
    }

    std::size_t AppendRecord(Bytes& container, std::string_view signature, std::uint32_t flags, ::FormID formId, const Bytes& body)
    {
        Require(body.size() <= 0xFFFFFFFF, "record test body exceeded the 32-bit size field");

        const auto offset = container.size();
        AppendSignature(container, signature);
        AppendUInt32(container, static_cast<std::uint32_t>(body.size()));
        AppendUInt32(container, flags);
        AppendUInt32(container, formId);
        AppendUInt32(container, 0);
        AppendUInt32(container, 0);
        AppendBytes(container, body);
        return offset;
    }

    std::array<std::byte, 4> TextLabel(std::string_view label)
    {
        Require(label.size() == 4, "test fixture used a non-four-byte group label");

        std::array<std::byte, 4> result {};
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = static_cast<std::byte>(static_cast<unsigned char>(label[index]));
        }

        return result;
    }

    std::size_t AppendGroup(Bytes& container, std::string_view label, std::uint32_t groupType, const Bytes& children)
    {
        Require(children.size() <= 0xFFFFFFFF - ::PluginEntryHeaderSize, "group test children exceeded the 32-bit size field");

        const auto offset = container.size();
        AppendSignature(container, "GRUP");
        AppendUInt32(container, static_cast<std::uint32_t>(::PluginEntryHeaderSize + children.size()));

        const auto labelBytes = TextLabel(label);
        container.insert(container.end(), labelBytes.begin(), labelBytes.end());

        AppendUInt32(container, groupType);
        AppendUInt32(container, 0);
        AppendUInt32(container, 0);
        AppendBytes(container, children);
        return offset;
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

    ::PluginFormIdResolver MasterAndSelfResolver()
    {
        return {
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
    }

    void RequireSuccess(const ::PluginPlanetScanOutput& output, std::string_view message)
    {
        Require(output.Succeeded(), message);
        Require(output.status == ::PluginPlanetScanStatus::Success, "successful plugin scan returned the wrong status");
    }

    void RequireFailure(const ::PluginPlanetScanOutput& output, ::PluginPlanetScanStatus expectedStatus, std::string_view message)
    {
        Require(output.status == expectedStatus, message);
        Require(!output.Succeeded(), "failed plugin scan reported success");
        Require(output.observations.empty(), "failed plugin scan exposed partial observations");
        Require(output.deletedBodyIds.empty(), "failed plugin scan exposed partial deletion tombstones");
    }

    void TestEmptyAndUnrelatedRecordsSucceed()
    {
        const auto resolver = FullSelfResolver();

        const Bytes emptyPlugin;
        const auto emptyOutput = ::ScanPluginPlanets(emptyPlugin, resolver);
        RequireSuccess(emptyOutput, "empty plugin byte span was rejected");
        Require(emptyOutput.observations.empty() && emptyOutput.deletedBodyIds.empty(), "empty plugin invented planet changes");

        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, {});
        AppendRecord(plugin, "STDT", CompressedRecordFlag, 0x00000400, {std::byte {0x01}, std::byte {0x02}});

        const auto unrelatedOutput = ::ScanPluginPlanets(plugin, resolver);
        RequireSuccess(unrelatedOutput, "unrelated records were rejected or decoded as PNDT");
        Require(unrelatedOutput.observations.empty() && unrelatedOutput.deletedBodyIds.empty(), "unrelated records produced planet changes");
    }

    void TestNestedUncompressedCompressedAndDeletedPlanets()
    {
        const auto resolver = MasterAndSelfResolver();

        Bytes deepestChildren;
        AppendRecord(deepestChildren, "PNDT", CompressedRecordFlag, 0x00000410, CompressBody(PlanetBody(9)));
        AppendRecord(deepestChildren, "PNDT", DeletedRecordFlag | CompressedRecordFlag, 0x01000420, {std::byte {0xAA}});

        Bytes innerGroup;
        AppendGroup(innerGroup, "PNDT", 0, deepestChildren);

        Bytes outerGroup;
        AppendGroup(outerGroup, "WRLD", 1, innerGroup);

        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, {});
        AppendRecord(plugin, "PNDT", 0, 0x01000400, PlanetBody(7));
        AppendBytes(plugin, outerGroup);

        const auto output = ::ScanPluginPlanets(plugin, resolver);

        RequireSuccess(output, "valid nested plugin planet scan failed");
        Require(output.observations.size() == 2, "valid nested plugin returned the wrong observation count");
        Require(output.observations[0].id == 0x05000400 && output.observations[0].systemId == 7, "root self-owned PNDT resolved incorrectly");
        Require(output.observations[1].id == 0x02000410 && output.observations[1].systemId == 9, "compressed nested master override resolved incorrectly");
        Require(output.deletedBodyIds.size() == 1 && output.deletedBodyIds[0] == 0x05000420, "deleted self-owned PNDT did not become a tombstone");
    }

    void TestDeletedMasterOverrideResolvesWithoutParsingBody()
    {
        const auto resolver = MasterAndSelfResolver();
        Bytes plugin;
        AppendRecord(plugin, "PNDT", DeletedRecordFlag | CompressedRecordFlag, 0x00000410, {std::byte {0xFF}});

        const auto output = ::ScanPluginPlanets(plugin, resolver);

        RequireSuccess(output, "valid deleted master override was rejected");
        Require(output.observations.empty(), "deleted PNDT produced an observation");
        Require(output.deletedBodyIds.size() == 1 && output.deletedBodyIds[0] == 0x02000410, "deleted master override resolved to the wrong runtime FormID");
    }

    void TestInvalidResolverFailsBeforeReadingEntries()
    {
        const ::PluginFormIdResolver resolver {
            {},
            {
                .tier = ::PluginTier::Small,
                .index = 0x1000,
            },
        };
        const Bytes malformedPlugin(3, std::byte {0xAA});

        const auto output = ::ScanPluginPlanets(malformedPlugin, resolver);

        RequireFailure(output, ::PluginPlanetScanStatus::InvalidResolver, "invalid resolver returned the wrong scanner status");
        Require(output.failureOffset == 0, "invalid resolver invented an entry failure offset");
        Require(output.entryReadResult == ::PluginEntryReadResult::End, "invalid resolver read malformed plugin entries");
    }

    void TestMalformedRootContainerClearsEarlierObservation()
    {
        const auto resolver = FullSelfResolver();
        Bytes plugin;
        AppendRecord(plugin, "PNDT", 0, 0x00000400, PlanetBody(7));
        const auto malformedOffset = plugin.size();
        plugin.insert(plugin.end(), {std::byte {0xAA}, std::byte {0xBB}, std::byte {0xCC}});

        const auto output = ::ScanPluginPlanets(plugin, resolver);

        RequireFailure(output, ::PluginPlanetScanStatus::MalformedEntryContainer, "malformed root entry container returned the wrong status");
        Require(output.entryReadResult == ::PluginEntryReadResult::TruncatedHeader, "root scanner did not preserve the malformed entry result");
        Require(output.failureOffset == malformedOffset, "root scanner reported the wrong absolute failure offset");
        Require(output.pluginRecordFormId == 0, "structural failure reported a PNDT FormID");
    }

    void TestMalformedNestedContainerReportsAbsoluteOffsetAndClearsChanges()
    {
        const auto resolver = FullSelfResolver();

        Bytes groupChildren;
        AppendRecord(groupChildren, "PNDT", DeletedRecordFlag, 0x00000420, {});
        const auto nestedMalformedOffset = groupChildren.size();
        groupChildren.insert(groupChildren.end(), {std::byte {0x01}, std::byte {0x02}, std::byte {0x03}});

        Bytes plugin;
        AppendRecord(plugin, "PNDT", 0, 0x00000400, PlanetBody(7));
        const auto groupOffset = AppendGroup(plugin, "PNDT", 0, groupChildren);
        const auto expectedFailureOffset = groupOffset + ::PluginEntryHeaderSize + nestedMalformedOffset;

        const auto output = ::ScanPluginPlanets(plugin, resolver);

        RequireFailure(output, ::PluginPlanetScanStatus::MalformedEntryContainer, "malformed nested entry container returned the wrong status");
        Require(output.entryReadResult == ::PluginEntryReadResult::TruncatedHeader, "nested scanner did not preserve the malformed entry result");
        Require(output.failureOffset == expectedFailureOffset, "nested scanner did not translate the child failure to an absolute plugin offset");
    }

    void TestRecordDecodeFailurePreservesDetailsAndClearsEarlierChanges()
    {
        const auto resolver = FullSelfResolver();
        Bytes plugin;
        AppendRecord(plugin, "PNDT", DeletedRecordFlag, 0x00000420, {});

        Bytes corruptCompressedBody;
        AppendUInt32(corruptCompressedBody, 8);
        corruptCompressedBody.insert(corruptCompressedBody.end(), {std::byte {0x01}, std::byte {0x02}, std::byte {0x03}});
        const auto failingRecordOffset = AppendRecord(plugin, "PNDT", CompressedRecordFlag, 0x00000430, corruptCompressedBody);

        const auto output = ::ScanPluginPlanets(plugin, resolver);

        RequireFailure(output, ::PluginPlanetScanStatus::RecordBodyDecodeFailed, "corrupt compressed PNDT returned the wrong scanner status");
        Require(output.recordBodyDecodeStatus == ::RecordBodyDecodeStatus::DecompressionFailed, "scanner did not preserve the decoder failure");
        Require(output.failureOffset == failingRecordOffset, "decoder failure reported the wrong record offset");
        Require(output.pluginRecordFormId == 0x00000430, "decoder failure reported the wrong plugin FormID");
    }

    void TestPlanetParseFailurePreservesDetailsAndClearsEarlierChanges()
    {
        const auto resolver = FullSelfResolver();
        Bytes plugin;
        AppendRecord(plugin, "PNDT", 0, 0x00000400, PlanetBody(7));

        Bytes missingGnamBody;
        AppendSubrecord(missingGnamBody, "EDID", {std::byte {'X'}, std::byte {0}});
        const auto failingRecordOffset = AppendRecord(plugin, "PNDT", 0, 0x00000410, missingGnamBody);

        const auto output = ::ScanPluginPlanets(plugin, resolver);

        RequireFailure(output, ::PluginPlanetScanStatus::PlanetDataParseFailed, "invalid PNDT body returned the wrong scanner status");
        Require(output.planetDataParseStatus == ::PlanetDataParseStatus::MissingGnam, "scanner did not preserve the planet-parser failure");
        Require(output.failureOffset == failingRecordOffset, "planet-parser failure reported the wrong record offset");
        Require(output.pluginRecordFormId == 0x00000410, "planet-parser failure reported the wrong plugin FormID");
    }

    void TestMalformedSubrecordDetailsArePreserved()
    {
        const auto resolver = FullSelfResolver();
        auto malformedBody = PlanetBody(7);
        const auto subrecordErrorOffset = malformedBody.size();
        malformedBody.insert(malformedBody.end(), {std::byte {0xAA}, std::byte {0xBB}, std::byte {0xCC}});

        Bytes plugin;
        const auto recordOffset = AppendRecord(plugin, "PNDT", 0, 0x00000400, malformedBody);

        const auto output = ::ScanPluginPlanets(plugin, resolver);

        RequireFailure(output, ::PluginPlanetScanStatus::PlanetDataParseFailed, "malformed PNDT subrecords returned the wrong scanner status");
        Require(output.planetDataParseStatus == ::PlanetDataParseStatus::MalformedSubrecordBody, "scanner lost the malformed-subrecord parser status");
        Require(output.subrecordReadResult == ::SubrecordReadResult::TruncatedHeader, "scanner lost the malformed subrecord reader result");
        Require(output.subrecordErrorOffset == subrecordErrorOffset, "scanner lost the decoded-body-relative subrecord offset");
        Require(output.failureOffset == recordOffset, "malformed subrecords reported the wrong plugin record offset");
    }

    void TestDuplicateObservationAndDeletionFailClosed()
    {
        const auto resolver = FullSelfResolver();
        Bytes plugin;
        AppendRecord(plugin, "PNDT", 0, 0x00000400, PlanetBody(7));
        const auto duplicateOffset = AppendRecord(plugin, "PNDT", DeletedRecordFlag, 0x00000400, {});

        const auto output = ::ScanPluginPlanets(plugin, resolver);

        RequireFailure(output, ::PluginPlanetScanStatus::DuplicateBodyId, "duplicate observation/deletion identity returned the wrong status");
        Require(output.failureOffset == duplicateOffset, "duplicate body reported the wrong second-record offset");
        Require(output.pluginRecordFormId == 0x00000400, "duplicate body reported the wrong plugin FormID");
    }

    void TestInvalidDeletedRecordIdsFailClosed()
    {
        const auto selfResolver = FullSelfResolver();
        Bytes zeroRecordPlugin;
        AppendRecord(zeroRecordPlugin, "PNDT", DeletedRecordFlag, 0, {});

        const auto zeroOutput = ::ScanPluginPlanets(zeroRecordPlugin, selfResolver);
        RequireFailure(zeroOutput, ::PluginPlanetScanStatus::PlanetDataParseFailed, "zero deleted PNDT FormID returned the wrong scanner status");
        Require(zeroOutput.planetDataParseStatus == ::PlanetDataParseStatus::ZeroRecordFormId, "zero deleted PNDT FormID lost its resolution failure");

        const auto masterResolver = MasterAndSelfResolver();
        Bytes unknownOwnerPlugin;
        AppendRecord(unknownOwnerPlugin, "PNDT", DeletedRecordFlag, 0x02000400, {});

        const auto unknownOutput = ::ScanPluginPlanets(unknownOwnerPlugin, masterResolver);
        RequireFailure(unknownOutput, ::PluginPlanetScanStatus::PlanetDataParseFailed, "unknown deleted PNDT owner slot returned the wrong scanner status");
        Require(unknownOutput.planetDataParseStatus == ::PlanetDataParseStatus::UnresolvableRecordFormId, "unknown deleted PNDT owner slot lost its resolution failure");
    }

    void RunTests()
    {
        TestEmptyAndUnrelatedRecordsSucceed();
        TestNestedUncompressedCompressedAndDeletedPlanets();
        TestDeletedMasterOverrideResolvesWithoutParsingBody();
        TestInvalidResolverFailsBeforeReadingEntries();
        TestMalformedRootContainerClearsEarlierObservation();
        TestMalformedNestedContainerReportsAbsoluteOffsetAndClearsChanges();
        TestRecordDecodeFailurePreservesDetailsAndClearsEarlierChanges();
        TestPlanetParseFailurePreservesDetailsAndClearsEarlierChanges();
        TestMalformedSubrecordDetailsArePreserved();
        TestDuplicateObservationAndDeletionFailClosed();
        TestInvalidDeletedRecordIdsFailClosed();
    }
}

void RunPluginPlanetScannerTests()
{
    RunTests();
}
