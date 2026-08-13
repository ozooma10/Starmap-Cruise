#include "Bodies/PluginPlanetIndexer.h"
#include "TestSuites.h"

#include <zlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
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

    Bytes PlanetBody(::FormID systemId)
    {
        Bytes gnam;
        AppendUInt32(gnam, systemId);
        AppendUInt32(gnam, 0x11111111);
        AppendUInt32(gnam, 0x22222222);

        Bytes body;
        AppendSubrecord(body, "GNAM", gnam);
        return body;
    }

    Bytes CompressBody(const Bytes& expandedBody)
    {
        Require(!expandedBody.empty(), "compressed test body was empty");
        Require(expandedBody.size() <= 0xFFFFFFFF, "compressed test body exceeded the expanded-size field");

        const auto inputSize = static_cast<uLong>(expandedBody.size());
        uLongf compressedSize = ::compressBound(inputSize);
        std::vector<Bytef> compressed(compressedSize);

        Require(::compress2(compressed.data(), &compressedSize, reinterpret_cast<const Bytef*>(expandedBody.data()), inputSize, Z_BEST_SPEED) == Z_OK, "zlib indexer fixture compression failed");

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

    void AppendGroup(Bytes& container, std::string_view label, std::uint32_t groupType, const Bytes& children)
    {
        Require(children.size() <= 0xFFFFFFFF - ::PluginEntryHeaderSize, "group test children exceeded the 32-bit size field");

        AppendSignature(container, "GRUP");
        AppendUInt32(container, static_cast<std::uint32_t>(::PluginEntryHeaderSize + children.size()));

        const auto labelBytes = TextLabel(label);
        container.insert(container.end(), labelBytes.begin(), labelBytes.end());

        AppendUInt32(container, groupType);
        AppendUInt32(container, 0);
        AppendUInt32(container, 0);
        AppendBytes(container, children);
    }

    std::vector<::ActivePluginIdentity> FullSelfPlugins()
    {
        return {
            {
                .name = "Self.esm",
                .identity = {
                    .tier = ::PluginTier::Full,
                    .index = 5,
                },
            },
        };
    }

    std::vector<::ActivePluginIdentity> MasterAndPatchPlugins()
    {
        return {
            {
                .name = "Master.esm",
                .identity =
                    {
                        .tier = ::PluginTier::Full,
                        .index = 2,
                    },
            },
            {
                .name = "Patch.esp",
                .identity = {
                    .tier = ::PluginTier::Full,
                    .index = 5,
                },
            },
        };
    }

    void RequireSuccess(const ::PluginPlanetIndexOutput& output, std::string_view message)
    {
        Require(output.Succeeded(), message);
        Require(output.status == ::PluginPlanetIndexStatus::Success, "successful plugin index returned the wrong status");
        Require(std::holds_alternative<std::monostate>(output.failure), "successful plugin index retained a failure payload");
    }

    void RequireFailure(const ::PluginPlanetIndexOutput& output, ::PluginPlanetIndexStatus expectedStatus, std::string_view message)
    {
        Require(output.status == expectedStatus, message);
        Require(!output.Succeeded(), "failed plugin index reported success");
        Require(output.observations.empty(), "failed plugin index exposed partial observations");
        Require(output.deletedBodyIds.empty(), "failed plugin index exposed partial deletion tombstones");
        Require(!std::holds_alternative<std::monostate>(output.failure), "failed plugin index did not retain a failure payload");
    }

    template <class Failure> const Failure& RequireFailurePayload(const ::PluginPlanetIndexOutput& output, std::string_view message)
    {
        const auto* failure = std::get_if<Failure>(&output.failure);
        Require(failure != nullptr, message);
        return *failure;
    }

    void TestHeaderOnlyPluginSucceedsWithoutChanges()
    {
        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, {});

        const auto activePlugins = FullSelfPlugins();
        const auto output = ::IndexPluginPlanets(plugin, activePlugins, "Self.esm");

        RequireSuccess(output, "valid header-only plugin was rejected");
        Require(output.observations.empty() && output.deletedBodyIds.empty(), "header-only plugin invented planet changes");
    }

    void TestSelfPluginReturnsObservationAndDeletionTombstone()
    {
        Bytes groupChildren;
        AppendRecord(groupChildren, "PNDT", 0, 0x00000400, PlanetBody(7));
        AppendRecord(groupChildren, "PNDT", DeletedRecordFlag, 0x00000410, {});

        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, {});
        AppendGroup(plugin, "PNDT", 0, groupChildren);

        const auto activePlugins = FullSelfPlugins();
        const auto output = ::IndexPluginPlanets(plugin, activePlugins, "SELF.ESM");

        RequireSuccess(output, "valid self plugin index failed");
        Require(output.observations.size() == 1, "self plugin returned the wrong observation count");
        Require(output.observations[0].id == 0x05000400 && output.observations[0].systemId == 7, "self plugin observation resolved incorrectly");
        Require(output.deletedBodyIds.size() == 1 && output.deletedBodyIds[0] == 0x05000410, "self plugin deletion resolved incorrectly");
    }

    void TestHeaderMastersResolveMasterOverrideAndSelfRecord()
    {
        Bytes tes4Body;
        AppendSubrecord(tes4Body, "MAST", TextPayload("Master.esm"));

        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, tes4Body);
        AppendRecord(plugin, "PNDT", 0, 0x00000410, PlanetBody(7));
        AppendRecord(plugin, "PNDT", 0, 0x01000420, PlanetBody(9));

        const auto activePlugins = MasterAndPatchPlugins();
        const auto output = ::IndexPluginPlanets(plugin, activePlugins, "Patch.esp");

        RequireSuccess(output, "valid master-override plugin index failed");
        Require(output.observations.size() == 2, "master-override plugin returned the wrong observation count");
        Require(output.observations[0].id == 0x02000410 && output.observations[0].systemId == 7, "master override resolved through the wrong owner");
        Require(output.observations[1].id == 0x05000420 && output.observations[1].systemId == 9, "self-owned patch record resolved through the wrong owner");
    }

    void TestCompressedCompactPluginWorksEndToEnd()
    {
        Bytes tes4Body;
        AppendSubrecord(tes4Body, "HEDR", {std::byte {0x01}});

        Bytes plugin;
        AppendRecord(plugin, "TES4", CompressedRecordFlag, 0, CompressBody(tes4Body));
        AppendRecord(plugin, "PNDT", CompressedRecordFlag, 0x00000678, CompressBody(PlanetBody(11)));

        const std::vector<::ActivePluginIdentity> activePlugins {
            {
                .name = "Compact.esl",
                .identity = {
                    .tier = ::PluginTier::Small,
                    .index = 2,
                },
            },
        };

        const auto output = ::IndexPluginPlanets(plugin, activePlugins, "Compact.esl");

        RequireSuccess(output, "compressed compact plugin index failed");
        Require(output.observations.size() == 1, "compressed compact plugin returned the wrong observation count");
        Require(output.observations[0].id == 0xFE002678 && output.observations[0].systemId == 11, "compressed compact plugin resolved incorrectly");
    }

    void TestHeaderFailurePayloadIsPreserved()
    {
        const Bytes plugin {
            std::byte {0xAA},
            std::byte {0xBB},
            std::byte {0xCC},
        };
        const auto activePlugins = FullSelfPlugins();

        const auto output = ::IndexPluginPlanets(plugin, activePlugins, "Self.esm");

        RequireFailure(output, ::PluginPlanetIndexStatus::HeaderParseFailed, "malformed plugin header returned the wrong index status");

        const auto& failure = RequireFailurePayload<::PluginHeaderParseOutput>(output, "header failure did not retain the header-parser output");
        Require(failure.status == ::PluginHeaderParseStatus::MalformedTes4Entry, "indexer lost the header-parser status");
        Require(failure.entryReadResult == ::PluginEntryReadResult::TruncatedHeader, "indexer lost the header entry-reader result");
        Require(failure.failureOffset == 0, "indexer changed the header failure offset");
    }

    void TestResolverFailurePreventsPlanetScan()
    {
        Bytes tes4Body;
        AppendSubrecord(tes4Body, "MAST", TextPayload("Missing.esm"));

        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, tes4Body);
        plugin.insert(plugin.end(), {std::byte {0xAA}, std::byte {0xBB}, std::byte {0xCC}});

        const std::vector<::ActivePluginIdentity> activePlugins {
            {
                .name = "Patch.esp",
                .identity = {
                    .tier = ::PluginTier::Full,
                    .index = 5,
                },
            },
        };

        const auto output = ::IndexPluginPlanets(plugin, activePlugins, "Patch.esp");

        RequireFailure(output, ::PluginPlanetIndexStatus::ResolverBuildFailed, "inactive master returned the wrong index status");

        const auto& failure = RequireFailurePayload<::PluginResolverBuildOutput>(output, "resolver failure did not retain the resolver-builder output");
        Require(failure.status == ::PluginResolverBuildStatus::MasterPluginNotActive, "indexer lost the resolver-builder status");
        Require(failure.masterIndex.has_value() && *failure.masterIndex == 0, "indexer lost the offending master index");
    }

    void TestPlanetScanFailureUsesAbsolutePluginOffsetAndClearsChanges()
    {
        Bytes plugin;
        AppendRecord(plugin, "TES4", 0, 0, {});
        AppendRecord(plugin, "PNDT", 0, 0x00000400, PlanetBody(7));
        const auto failingRecordOffset = AppendRecord(plugin, "PNDT", 0, 0x00000410, {});

        const auto activePlugins = FullSelfPlugins();
        const auto output = ::IndexPluginPlanets(plugin, activePlugins, "Self.esm");

        RequireFailure(output, ::PluginPlanetIndexStatus::PlanetScanFailed, "invalid PNDT returned the wrong index status");

        const auto& failure = RequireFailurePayload<::PluginPlanetScanOutput>(output, "scan failure did not retain the planet-scanner output");
        Require(failure.status == ::PluginPlanetScanStatus::PlanetDataParseFailed, "indexer lost the planet-scanner status");
        Require(failure.planetDataParseStatus == ::PlanetDataParseStatus::MissingGnam, "indexer lost the planet-parser status");
        Require(failure.failureOffset == failingRecordOffset, "indexer did not normalize the scanner offset to the complete plugin");
        Require(failure.pluginRecordFormId == 0x00000410, "indexer lost the failing plugin FormID");
        Require(failure.observations.empty() && failure.deletedBodyIds.empty(), "retained scan failure exposed partial plugin changes");
    }

    void TestSucceededRequiresAnEmptyFailureVariant()
    {
        ::PluginPlanetIndexOutput output {
            .status = ::PluginPlanetIndexStatus::Success,
            .failure = ::PluginHeaderParseOutput {
                .status = ::PluginHeaderParseStatus::MissingTes4,
            },
        };

        Require(!output.Succeeded(), "success status with a failure payload reported success");
    }

    void RunTests()
    {
        TestHeaderOnlyPluginSucceedsWithoutChanges();
        TestSelfPluginReturnsObservationAndDeletionTombstone();
        TestHeaderMastersResolveMasterOverrideAndSelfRecord();
        TestCompressedCompactPluginWorksEndToEnd();
        TestHeaderFailurePayloadIsPreserved();
        TestResolverFailurePreventsPlanetScan();
        TestPlanetScanFailureUsesAbsolutePluginOffsetAndClearsChanges();
        TestSucceededRequiresAnEmptyFailureVariant();
    }
}

void RunPluginPlanetIndexerTests()
{
    RunTests();
}
