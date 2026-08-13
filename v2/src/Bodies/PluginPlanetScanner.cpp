#include "Bodies/PluginPlanetScanner.h"

#include <optional>
#include <unordered_set>
#include <variant>
#include <vector>

namespace
{
    constexpr std::uint32_t DeletedRecordFlag = 0x00000020;
    constexpr std::uint32_t CompressedRecordFlag = 0x00040000;

    struct EntryContainerFrame
    {
        EntryContainerFrame(std::span<const std::byte> entries, std::size_t baseOffset) : reader(entries), baseOffset(baseOffset) {}

        PluginEntryReader reader;
        std::size_t baseOffset {0};
    };

    PluginPlanetScanOutput Failure(PluginPlanetScanStatus status)
    {
        return {
            .status = status,
        };
    }

    void SetFailure(PluginPlanetScanOutput& output, PluginPlanetScanStatus status, std::size_t failureOffset, FormID pluginRecordFormId = 0)
    {
        output.status = status;
        output.observations.clear();
        output.deletedBodyIds.clear();
        output.failureOffset = failureOffset;
        output.pluginRecordFormId = pluginRecordFormId;
    }

    std::size_t CurrentEntryOffset(const EntryContainerFrame& frame, std::size_t entrySize)
    {
        return frame.baseOffset + frame.reader.Offset() - entrySize;
    }

    std::optional<FormID> ResolveDeletedBodyId(FormID pluginRecordFormId, const PluginFormIdResolver& resolver, PlanetDataParseStatus& failureStatus)
    {
        if (pluginRecordFormId == 0) {
            failureStatus = PlanetDataParseStatus::ZeroRecordFormId;
            return std::nullopt;
        }

        const auto resolved = resolver.Resolve(pluginRecordFormId);

        if (!resolved || *resolved == 0) {
            failureStatus = PlanetDataParseStatus::UnresolvableRecordFormId;
            return std::nullopt;
        }

        return resolved;
    }
}

bool PluginPlanetScanOutput::Succeeded() const
{
    return status == PluginPlanetScanStatus::Success;
}

PluginPlanetScanOutput ScanPluginPlanets(std::span<const std::byte> pluginBytes, const PluginFormIdResolver& resolver)
{
    if (!resolver.IsValid()) {
        return Failure(PluginPlanetScanStatus::InvalidResolver);
    }

    PluginPlanetScanOutput output {
        .status = PluginPlanetScanStatus::Success,
    };

    std::unordered_set<FormID> encounteredBodyIds;
    std::vector<std::byte> decompressionBuffer;

    std::vector<EntryContainerFrame> frames;
    frames.emplace_back(pluginBytes, 0);

    PluginEntryView entry;

    while (!frames.empty()) {
        auto& frame = frames.back();
        const auto readResult = frame.reader.Next(entry);

        if (readResult == PluginEntryReadResult::End) {
            frames.pop_back();
            continue;
        }

        if (readResult != PluginEntryReadResult::Record && readResult != PluginEntryReadResult::Group) {
            SetFailure(output, PluginPlanetScanStatus::MalformedEntryContainer, frame.baseOffset + frame.reader.ErrorOffset());

            output.entryReadResult = readResult;
            return output;
        }

        if (readResult == PluginEntryReadResult::Group) {
            const auto group = std::get<PluginGroupView>(entry);
            const auto groupSize = PluginEntryHeaderSize + group.children.size();
            const auto groupOffset = CurrentEntryOffset(frame, groupSize);

            frames.emplace_back(group.children, groupOffset + PluginEntryHeaderSize);

            continue;
        }

        const auto record = std::get<PluginRecordView>(entry);
        const auto recordSize = PluginEntryHeaderSize + record.storedBody.size();
        const auto recordOffset = CurrentEntryOffset(frame, recordSize);

        if (!record.HasSignature("PNDT")) {
            continue;
        }

        if ((record.flags & DeletedRecordFlag) != 0) {
            PlanetDataParseStatus resolutionFailure {PlanetDataParseStatus::Success};

            const auto bodyId = ResolveDeletedBodyId(record.formId, resolver, resolutionFailure);

            if (!bodyId) {
                SetFailure(output, PluginPlanetScanStatus::PlanetDataParseFailed, recordOffset, record.formId);

                output.planetDataParseStatus = resolutionFailure;
                return output;
            }

            if (!encounteredBodyIds.insert(*bodyId).second) {
                SetFailure(output, PluginPlanetScanStatus::DuplicateBodyId, recordOffset, record.formId);

                return output;
            }

            output.deletedBodyIds.push_back(*bodyId);
            continue;
        }

        const bool compressed = (record.flags & CompressedRecordFlag) != 0;

        const auto decoded = DecodeRecordBody(record.storedBody, compressed, decompressionBuffer);

        if (!decoded.Succeeded()) {
            SetFailure(output, PluginPlanetScanStatus::RecordBodyDecodeFailed, recordOffset, record.formId);

            output.recordBodyDecodeStatus = decoded.status;
            return output;
        }

        const auto parsed = ParsePlanetData(record.formId, decoded.body, resolver);

        if (!parsed.Succeeded()) {
            SetFailure(output, PluginPlanetScanStatus::PlanetDataParseFailed, recordOffset, record.formId);

            output.planetDataParseStatus = parsed.status;
            output.subrecordReadResult = parsed.subrecordReadResult;
            output.subrecordErrorOffset = parsed.subrecordErrorOffset;

            return output;
        }

        const auto observation = *parsed.observation;

        if (!encounteredBodyIds.insert(observation.id).second) {
            SetFailure(output, PluginPlanetScanStatus::DuplicateBodyId, recordOffset, record.formId);

            return output;
        }

        output.observations.push_back(observation);
    }

    return output;
}