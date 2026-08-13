#include "Bodies/PluginEntryReader.h"
#include "TestSuites.h"

#include <algorithm>
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

    void AppendRecord(Bytes& container, std::string_view signature, std::uint32_t flags, ::FormID formId, const Bytes& body)
    {
        Require(body.size() <= 0xFFFFFFFF, "record test body exceeded the 32-bit size field");

        AppendSignature(container, signature);
        AppendUInt32(container, static_cast<std::uint32_t>(body.size()));
        AppendUInt32(container, flags);
        AppendUInt32(container, formId);
        AppendUInt32(container, 0);
        AppendUInt32(container, 0);
        AppendBytes(container, body);
    }

    void AppendGroupHeader(Bytes& container, std::uint32_t groupSize, const std::array<std::byte, 4>& label, std::uint32_t groupType)
    {
        AppendSignature(container, "GRUP");
        AppendUInt32(container, groupSize);
        container.insert(container.end(), label.begin(), label.end());
        AppendUInt32(container, groupType);
        AppendUInt32(container, 0);
        AppendUInt32(container, 0);
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

    void AppendGroup(Bytes& container, const std::array<std::byte, 4>& label, std::uint32_t groupType, const Bytes& children)
    {
        Require(children.size() <= 0xFFFFFFFF - ::PluginEntryHeaderSize, "group test children exceeded the 32-bit size field");

        AppendGroupHeader(container, static_cast<std::uint32_t>(::PluginEntryHeaderSize + children.size()), label, groupType);
        AppendBytes(container, children);
    }

    const ::PluginRecordView& RequireRecord(const ::PluginEntryView& entry, std::string_view message)
    {
        const auto* record = std::get_if<::PluginRecordView>(&entry);
        Require(record != nullptr, message);
        return *record;
    }

    const ::PluginGroupView& RequireGroup(const ::PluginEntryView& entry, std::string_view message)
    {
        const auto* group = std::get_if<::PluginGroupView>(&entry);
        Require(group != nullptr, message);
        return *group;
    }

    void RequireMonostate(const ::PluginEntryView& entry, std::string_view message)
    {
        Require(std::holds_alternative<std::monostate>(entry), message);
    }

    void TestReadsRecordEnvelopeAndStoredBody()
    {
        const Bytes expectedBody {
            std::byte {0x10},
            std::byte {0x20},
            std::byte {0x30},
        };
        Bytes container;
        AppendRecord(container, "PNDT", 0x00040000, 0x02001234, expectedBody);

        ::PluginEntryReader reader {container};
        ::PluginEntryView entry;

        Require(reader.Next(entry) == ::PluginEntryReadResult::Record, "valid record did not return Record");

        const auto& record = RequireRecord(entry, "valid record did not expose a record view");
        Require(record.HasSignature("PNDT"), "record signature was not preserved");
        Require(!record.HasSignature("pndt"), "record signature comparison ignored case");
        Require(!record.HasSignature("PND"), "record signature comparison accepted the wrong length");
        Require(record.flags == 0x00040000, "record flags were not read little-endian");
        Require(record.formId == 0x02001234, "record FormID was not read little-endian");
        Require(record.storedBody.size() == expectedBody.size(), "record view returned the wrong stored-body size");
        Require(record.storedBody.data() == container.data() + ::PluginEntryHeaderSize, "record view did not borrow the exact stored body");
        Require(std::equal(record.storedBody.begin(), record.storedBody.end(), expectedBody.begin()), "record view changed stored-body bytes");
        Require(reader.Offset() == container.size(), "record read advanced to the wrong offset");
        Require(reader.ErrorOffset() == 0, "successful record read reported an error offset");
    }

    void TestReadsZeroLengthRecord()
    {
        Bytes container;
        AppendRecord(container, "TES4", 0, 0, {});

        ::PluginEntryReader reader {container};
        ::PluginEntryView entry;

        Require(reader.Next(entry) == ::PluginEntryReadResult::Record, "zero-length record was rejected");
        Require(RequireRecord(entry, "zero-length record did not expose a record view").storedBody.empty(), "zero-length record exposed body bytes");
        Require(reader.Offset() == ::PluginEntryHeaderSize, "zero-length record advanced by the wrong size");
    }

    void TestReadsSiblingEntriesAndEndIsTerminal()
    {
        Bytes container;
        AppendRecord(container, "TES4", 1, 0, {std::byte {0xAA}});
        const auto secondOffset = container.size();
        AppendRecord(container, "PNDT", 2, 0x00000042, {std::byte {0xBB}, std::byte {0xCC}});

        ::PluginEntryReader reader {container};
        ::PluginEntryView entry;

        Require(reader.Next(entry) == ::PluginEntryReadResult::Record, "first sibling record was not read");
        Require(RequireRecord(entry, "first sibling did not expose a record view").HasSignature("TES4"), "first sibling signature changed");
        Require(reader.Offset() == secondOffset, "first sibling advanced to the wrong boundary");

        Require(reader.Next(entry) == ::PluginEntryReadResult::Record, "second sibling record was not read");
        Require(RequireRecord(entry, "second sibling did not expose a record view").HasSignature("PNDT"), "second sibling signature changed");
        Require(reader.Offset() == container.size(), "second sibling advanced to the wrong boundary");

        Require(reader.Next(entry) == ::PluginEntryReadResult::End, "reader did not return End at the exact container boundary");
        RequireMonostate(entry, "End retained the previous sibling view");

        entry = ::PluginRecordView {};
        Require(reader.Next(entry) == ::PluginEntryReadResult::End, "End was not terminal");
        RequireMonostate(entry, "terminal End did not clear the output view");
    }

    void TestReadsTextLabeledGroup()
    {
        Bytes children;
        AppendRecord(children, "PNDT", 0, 0x00000123, {std::byte {0x42}});

        Bytes container;
        AppendGroup(container, TextLabel("PNDT"), 0, children);

        ::PluginEntryReader reader {container};
        ::PluginEntryView entry;

        Require(reader.Next(entry) == ::PluginEntryReadResult::Group, "valid group did not return Group");

        const auto& group = RequireGroup(entry, "valid group did not expose a group view");
        Require(group.HasLabel("PNDT"), "text group label was not preserved");
        Require(!group.HasLabel("pndt"), "group label comparison ignored case");
        Require(!group.HasLabel("PND"), "group label comparison accepted the wrong length");
        Require(group.groupType == 0, "group type changed");
        Require(group.children.size() == children.size(), "group view returned the wrong child size");
        Require(group.children.data() == container.data() + ::PluginEntryHeaderSize, "group view did not exclude its own header");
        Require(std::equal(group.children.begin(), group.children.end(), children.begin()), "group view changed child bytes");
        Require(reader.Offset() == container.size(), "group read advanced to the wrong offset");
    }

    void TestReadsEmptyAndBinaryLabeledGroups()
    {
        const std::array<std::byte, 4> binaryLabel {
            std::byte {0x78},
            std::byte {0x56},
            std::byte {0x34},
            std::byte {0x12},
        };
        Bytes container;
        AppendGroup(container, binaryLabel, 1, {});

        ::PluginEntryReader reader {container};
        ::PluginEntryView entry;

        Require(reader.Next(entry) == ::PluginEntryReadResult::Group, "empty binary-labeled group was rejected");

        const auto& group = RequireGroup(entry, "empty binary-labeled group did not expose a group view");
        Require(group.label == binaryLabel, "binary group label was not preserved byte-for-byte");
        Require(group.groupType == 1, "binary-labeled group type changed");
        Require(group.children.empty(), "empty group exposed child bytes");
        Require(!group.HasLabel("PNDT"), "binary group label matched unrelated text");
        Require(reader.Offset() == ::PluginEntryHeaderSize, "empty group advanced by the wrong size");
    }

    void TestNestedGroupsUseIndependentReaders()
    {
        Bytes recordBytes;
        AppendRecord(recordBytes, "PNDT", 0, 0x00000077, {std::byte {0xAB}});

        Bytes innerGroupBytes;
        AppendGroup(innerGroupBytes, TextLabel("PNDT"), 0, recordBytes);

        Bytes outerGroupBytes;
        AppendGroup(outerGroupBytes, TextLabel("WRLD"), 1, innerGroupBytes);

        ::PluginEntryReader outerReader {outerGroupBytes};
        ::PluginEntryView outerEntry;
        Require(outerReader.Next(outerEntry) == ::PluginEntryReadResult::Group, "outer group was not read");

        const auto& outerGroup = RequireGroup(outerEntry, "outer group did not expose a group view");
        ::PluginEntryReader innerReader {outerGroup.children};
        ::PluginEntryView innerEntry;
        Require(innerReader.Next(innerEntry) == ::PluginEntryReadResult::Group, "nested group was not read from outer children");

        const auto& innerGroup = RequireGroup(innerEntry, "nested group did not expose a group view");
        ::PluginEntryReader recordReader {innerGroup.children};
        ::PluginEntryView recordEntry;
        Require(recordReader.Next(recordEntry) == ::PluginEntryReadResult::Record, "record was not read from nested group children");

        const auto& record = RequireRecord(recordEntry, "nested record did not expose a record view");
        Require(record.HasSignature("PNDT") && record.formId == 0x00000077, "nested record envelope changed");
        Require(recordReader.Next(recordEntry) == ::PluginEntryReadResult::End, "nested record reader did not end at its child boundary");
    }

    void TestTruncatedHeaderFailsTerminally()
    {
        const Bytes container(23, std::byte {0});
        ::PluginEntryReader reader {container};
        ::PluginEntryView entry = ::PluginRecordView {};

        Require(reader.Next(entry) == ::PluginEntryReadResult::TruncatedHeader, "partial entry header returned the wrong failure");
        RequireMonostate(entry, "truncated header retained a stale output view");
        Require(reader.Offset() == 0, "truncated header advanced the reader");
        Require(reader.ErrorOffset() == 0, "truncated header reported the wrong error offset");

        entry = ::PluginGroupView {};
        Require(reader.Next(entry) == ::PluginEntryReadResult::TruncatedHeader, "truncated-header failure was not terminal");
        RequireMonostate(entry, "terminal truncated-header failure retained a stale output view");
    }

    void TestTruncatedRecordBodyFails()
    {
        Bytes container;
        AppendSignature(container, "PNDT");
        AppendUInt32(container, 3);
        AppendUInt32(container, 0);
        AppendUInt32(container, 0x00000042);
        AppendUInt32(container, 0);
        AppendUInt32(container, 0);
        container.push_back(std::byte {0xAA});
        container.push_back(std::byte {0xBB});

        ::PluginEntryReader reader {container};
        ::PluginEntryView entry;

        Require(reader.Next(entry) == ::PluginEntryReadResult::TruncatedRecordBody, "truncated record body returned the wrong failure");
        RequireMonostate(entry, "truncated record body exposed a partial record view");
        Require(reader.Offset() == 0, "truncated record body advanced the reader");
        Require(reader.ErrorOffset() == 0, "truncated record body reported the wrong error offset");
    }

    void TestInvalidGroupSizesFail()
    {
        for (const std::uint32_t declaredSize : {0U, static_cast<std::uint32_t>(::PluginEntryHeaderSize - 1)}) {
            Bytes container;
            AppendGroupHeader(container, declaredSize, TextLabel("PNDT"), 0);

            ::PluginEntryReader reader {container};
            ::PluginEntryView entry;

            Require(reader.Next(entry) == ::PluginEntryReadResult::InvalidGroupSize, "undersized group returned the wrong failure");
            RequireMonostate(entry, "undersized group exposed a partial group view");
            Require(reader.Offset() == 0, "undersized group advanced the reader");
            Require(reader.ErrorOffset() == 0, "undersized group reported the wrong error offset");
        }
    }

    void TestTruncatedGroupFails()
    {
        Bytes container;
        AppendGroupHeader(container, static_cast<std::uint32_t>(::PluginEntryHeaderSize + 1), TextLabel("PNDT"), 0);

        ::PluginEntryReader reader {container};
        ::PluginEntryView entry;

        Require(reader.Next(entry) == ::PluginEntryReadResult::TruncatedGroup, "truncated group returned the wrong failure");
        RequireMonostate(entry, "truncated group exposed a partial group view");
        Require(reader.Offset() == 0, "truncated group advanced the reader");
        Require(reader.ErrorOffset() == 0, "truncated group reported the wrong error offset");
    }

    void TestFailureAfterValidEntryReportsItsOwnOffset()
    {
        Bytes container;
        AppendRecord(container, "TES4", 0, 0, {std::byte {0x01}});
        const auto malformedOffset = container.size();
        container.push_back(std::byte {0xAA});
        container.push_back(std::byte {0xBB});
        container.push_back(std::byte {0xCC});

        ::PluginEntryReader reader {container};
        ::PluginEntryView entry;

        Require(reader.Next(entry) == ::PluginEntryReadResult::Record, "valid entry before malformed bytes was not read");
        Require(reader.Next(entry) == ::PluginEntryReadResult::TruncatedHeader, "malformed second entry returned the wrong failure");
        RequireMonostate(entry, "malformed second entry retained the first record view");
        Require(reader.Offset() == malformedOffset, "malformed second entry changed the last valid boundary");
        Require(reader.ErrorOffset() == malformedOffset, "malformed second entry reported the wrong error offset");
    }

    void RunTests()
    {
        static_assert(::PluginEntryHeaderSize == 24);

        TestReadsRecordEnvelopeAndStoredBody();
        TestReadsZeroLengthRecord();
        TestReadsSiblingEntriesAndEndIsTerminal();
        TestReadsTextLabeledGroup();
        TestReadsEmptyAndBinaryLabeledGroups();
        TestNestedGroupsUseIndependentReaders();
        TestTruncatedHeaderFailsTerminally();
        TestTruncatedRecordBodyFails();
        TestInvalidGroupSizesFail();
        TestTruncatedGroupFails();
        TestFailureAfterValidEntryReportsItsOwnOffset();
    }
}

void RunPluginEntryReaderTests()
{
    RunTests();
}
