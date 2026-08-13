#include "Bodies/RecordBodyDecoder.h"
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

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    void AppendUInt32(Bytes& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFF));
        bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
        bytes.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
        bytes.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
    }

    Bytes Compress(const Bytes& expandedBody, std::uint32_t declaredExpandedSize)
    {
        const auto inputSize = static_cast<uLong>(expandedBody.size());
        uLongf compressedSize = ::compressBound(inputSize);
        std::vector<Bytef> compressed(compressedSize);

        Require(::compress2(compressed.data(), &compressedSize, reinterpret_cast<const Bytef*>(expandedBody.data()), inputSize, Z_BEST_SPEED) == Z_OK, "zlib test fixture compression failed");

        compressed.resize(compressedSize);

        Bytes storedBody;
        AppendUInt32(storedBody, declaredExpandedSize);
        storedBody.insert(storedBody.end(), reinterpret_cast<const std::byte*>(compressed.data()), reinterpret_cast<const std::byte*>(compressed.data() + compressed.size()));
        return storedBody;
    }

    Bytes Compress(const Bytes& expandedBody)
    {
        Require(expandedBody.size() <= 0xFFFFFFFF, "zlib test fixture exceeded the record size field");

        return Compress(expandedBody, static_cast<std::uint32_t>(expandedBody.size()));
    }

    bool BodyEquals(const ::RecordBodyDecodeOutput& output, const Bytes& expected)
    {
        return output.body.size() == expected.size() && std::equal(output.body.begin(), output.body.end(), expected.begin());
    }

    void TestUncompressedBodyIsBorrowed()
    {
        const Bytes storedBody {
            std::byte {0x10},
            std::byte {0x20},
            std::byte {0x30},
        };
        Bytes buffer {
            std::byte {0xFF},
        };

        const auto output = ::DecodeRecordBody(storedBody, false, buffer);

        Require(output.Succeeded(), "uncompressed body was rejected");
        Require(output.status == ::RecordBodyDecodeStatus::Success, "uncompressed body returned the wrong status");
        Require(BodyEquals(output, storedBody), "uncompressed output changed the stored bytes");
        Require(output.body.data() == storedBody.data(), "uncompressed body was copied instead of borrowed");
        Require(buffer.empty(), "uncompressed decode retained stale scratch bytes");
    }

    void TestEmptyUncompressedBodySucceeds()
    {
        const Bytes storedBody;
        Bytes buffer {
            std::byte {0xAA},
        };

        const auto output = ::DecodeRecordBody(storedBody, false, buffer);

        Require(output.Succeeded(), "empty uncompressed body was rejected");
        Require(output.body.empty(), "empty uncompressed body gained bytes");
        Require(buffer.empty(), "empty uncompressed decode retained stale scratch bytes");
    }

    void TestCompressedBodyIsExpandedIntoBuffer()
    {
        Bytes expandedBody;
        expandedBody.reserve(4096);
        for (std::size_t index = 0; index < 4096; ++index) {
            expandedBody.push_back(static_cast<std::byte>(index % 251));
        }

        const auto storedBody = Compress(expandedBody);
        Bytes buffer;

        const auto output = ::DecodeRecordBody(storedBody, true, buffer);

        Require(output.Succeeded(), "valid compressed body was rejected");
        Require(BodyEquals(output, expandedBody), "compressed body expanded to the wrong bytes");
        Require(output.body.data() == buffer.data(), "compressed output did not borrow the scratch buffer");
        Require(buffer.size() == expandedBody.size(), "scratch buffer retained the wrong expanded size");
    }

    void TestMissingExpandedSizeFails()
    {
        const Bytes storedBody {
            std::byte {0x01},
            std::byte {0x02},
            std::byte {0x03},
        };
        Bytes buffer {
            std::byte {0xAA},
        };

        const auto output = ::DecodeRecordBody(storedBody, true, buffer);

        Require(output.status == ::RecordBodyDecodeStatus::MissingExpandedSize, "truncated expanded-size field was accepted");
        Require(!output.Succeeded(), "missing expanded size reported success");
        Require(output.body.empty(), "missing expanded size exposed a body");
        Require(buffer.empty(), "missing expanded size retained stale scratch bytes");
    }

    void TestZeroExpandedSizeFails()
    {
        Bytes storedBody;
        AppendUInt32(storedBody, 0);
        storedBody.push_back(std::byte {0x78});

        Bytes buffer;
        const auto output = ::DecodeRecordBody(storedBody, true, buffer);

        Require(output.status == ::RecordBodyDecodeStatus::InvalidExpandedSize, "zero expanded size was accepted");
        Require(output.body.empty(), "zero expanded size exposed a body");
    }

    void TestExpandedSizeLimitFailsBeforeAllocation()
    {
        Bytes storedBody;
        AppendUInt32(storedBody, static_cast<std::uint32_t>(::MaximumExpandedRecordBodySize + 1));
        storedBody.push_back(std::byte {0x78});

        Bytes buffer {
            std::byte {0xBB},
        };
        const auto output = ::DecodeRecordBody(storedBody, true, buffer);

        Require(output.status == ::RecordBodyDecodeStatus::ExpandedSizeTooLarge, "oversized expanded body was accepted");
        Require(buffer.empty(), "oversized expanded body modified the scratch allocation");
    }

    void TestMissingCompressedPayloadFails()
    {
        Bytes storedBody;
        AppendUInt32(storedBody, 4);

        Bytes buffer;
        const auto output = ::DecodeRecordBody(storedBody, true, buffer);

        Require(output.status == ::RecordBodyDecodeStatus::MissingCompressedPayload, "missing compressed payload was accepted");
        Require(output.body.empty(), "missing compressed payload exposed a body");
    }

    void TestCorruptCompressedPayloadFails()
    {
        Bytes storedBody;
        AppendUInt32(storedBody, 8);
        storedBody.insert(
            storedBody.end(),
            {
                std::byte {0x01},
                std::byte {0x02},
                std::byte {0x03},
                std::byte {0x04},
            }
        );

        Bytes buffer;
        const auto output = ::DecodeRecordBody(storedBody, true, buffer);

        Require(output.status == ::RecordBodyDecodeStatus::DecompressionFailed, "corrupt zlib payload was accepted");
        Require(output.body.empty(), "corrupt zlib payload exposed a body");
        Require(buffer.empty(), "failed zlib decode retained partial output");
    }

    void TestDeclaredExpandedSizeMustMatchOutput()
    {
        const Bytes expandedBody {
            std::byte {0x11},
            std::byte {0x22},
            std::byte {0x33},
        };
        const auto storedBody = Compress(expandedBody, 4);

        Bytes buffer;
        const auto output = ::DecodeRecordBody(storedBody, true, buffer);

        Require(output.status == ::RecordBodyDecodeStatus::ExpandedSizeMismatch, "incorrect declared expanded size was accepted");
        Require(output.body.empty(), "expanded-size mismatch exposed a body");
        Require(buffer.empty(), "expanded-size mismatch retained decoded bytes");
    }

    void TestTrailingCompressedDataFails()
    {
        const Bytes expandedBody {
            std::byte {0x11},
            std::byte {0x22},
            std::byte {0x33},
        };
        auto storedBody = Compress(expandedBody);
        storedBody.push_back(std::byte {0xCC});

        Bytes buffer;
        const auto output = ::DecodeRecordBody(storedBody, true, buffer);

        Require(output.status == ::RecordBodyDecodeStatus::TrailingCompressedData, "bytes after the zlib stream were accepted");
        Require(output.body.empty(), "trailing compressed data exposed a body");
        Require(buffer.empty(), "trailing compressed data retained decoded bytes");
    }

    void TestLaterDecodeReusesAndClearsBuffer()
    {
        const Bytes expandedBody {
            std::byte {0x41},
            std::byte {0x42},
        };
        const auto compressedBody = Compress(expandedBody);

        Bytes buffer;
        const auto compressedOutput = ::DecodeRecordBody(compressedBody, true, buffer);
        Require(compressedOutput.Succeeded() && !buffer.empty(), "buffer-reuse setup decode failed");

        const Bytes uncompressedBody {
            std::byte {0x55},
        };
        const auto uncompressedOutput = ::DecodeRecordBody(uncompressedBody, false, buffer);

        Require(uncompressedOutput.Succeeded(), "uncompressed decode after compressed decode failed");
        Require(buffer.empty(), "later uncompressed decode retained earlier expanded bytes");
        Require(uncompressedOutput.body.data() == uncompressedBody.data(), "later uncompressed decode returned stale scratch storage");
    }

    void RunTests()
    {
        TestUncompressedBodyIsBorrowed();
        TestEmptyUncompressedBodySucceeds();
        TestCompressedBodyIsExpandedIntoBuffer();
        TestMissingExpandedSizeFails();
        TestZeroExpandedSizeFails();
        TestExpandedSizeLimitFailsBeforeAllocation();
        TestMissingCompressedPayloadFails();
        TestCorruptCompressedPayloadFails();
        TestDeclaredExpandedSizeMustMatchOutput();
        TestTrailingCompressedDataFails();
        TestLaterDecodeReusesAndClearsBuffer();
    }
}

void RunRecordBodyDecoderTests()
{
    RunTests();
}
