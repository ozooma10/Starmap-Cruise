#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

namespace CFS::Engine
{
    struct ExecutableImage
    {
        std::uintptr_t base{ 0 };
        std::size_t size{ 0 };

        [[nodiscard]] bool Contains(std::uintptr_t a_address,
            std::size_t a_span = 1) const noexcept;
        [[nodiscard]] std::uintptr_t Rva(std::uintptr_t a_address) const noexcept;
    };

    [[nodiscard]] std::optional<ExecutableImage> CurrentExecutable();
    [[nodiscard]] bool IsReadableRange(std::uintptr_t a_address,
        std::size_t a_size) noexcept;

    template <class T>
    [[nodiscard]] bool ReadMemory(std::uintptr_t a_address, T& a_value) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!IsReadableRange(a_address, sizeof(T)))
            return false;
        std::memcpy(&a_value, reinterpret_cast<const void*>(a_address), sizeof(T));
        return true;
    }

    [[nodiscard]] std::string HexBytes(std::span<const std::uint8_t> a_bytes);
}
