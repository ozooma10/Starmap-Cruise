#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace CFS::V2::Native
{
    struct ExecutableImage
    {
        std::uintptr_t base {0};
        std::size_t size {0};

        bool Contains(std::uintptr_t address, std::size_t length) const;
    };

    ExecutableImage CurrentExecutable();
    bool IsReadable(std::uintptr_t address, std::size_t length);
    bool IsExecutable(std::uintptr_t address, std::size_t length);
    bool Matches(std::uintptr_t address, std::span<const std::uint8_t> expected);
    std::uintptr_t DecodeRelativeCall(std::uintptr_t address);
}
