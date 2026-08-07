#include "Engine/RuntimeMemory.h"

#include <Windows.h>
#undef ERROR

#include <algorithm>
#include <format>

namespace CFS::Engine
{
    bool ExecutableImage::Contains(std::uintptr_t a_address,
        std::size_t a_span) const noexcept
    {
        if (!base || !size || a_address < base || a_address > UINTPTR_MAX - a_span)
            return false;
        const auto end = base + size;
        return end >= base && a_address < end && a_address + a_span <= end;
    }

    std::uintptr_t ExecutableImage::Rva(std::uintptr_t a_address) const noexcept
    {
        return a_address >= base ? a_address - base : 0;
    }

    bool IsReadableRange(std::uintptr_t a_address, std::size_t a_size) noexcept
    {
        if (!a_address || !a_size || a_address > UINTPTR_MAX - a_size)
            return false;

        const auto end = a_address + a_size;
        while (a_address < end) {
            MEMORY_BASIC_INFORMATION memory{};
            if (::VirtualQuery(reinterpret_cast<const void*>(a_address),
                    &memory, sizeof(memory)) != sizeof(memory) ||
                memory.State != MEM_COMMIT ||
                (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }

            const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) +
                                   memory.RegionSize;
            if (regionEnd <= a_address)
                return false;
            a_address = std::min(end, regionEnd);
        }
        return true;
    }

    std::optional<ExecutableImage> CurrentExecutable()
    {
        const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
        IMAGE_DOS_HEADER dos{};
        if (!base || !ReadMemory(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew < 0) {
            return std::nullopt;
        }

        const auto ntAddress = base + static_cast<std::uintptr_t>(dos.e_lfanew);
        if (ntAddress < base)
            return std::nullopt;
        IMAGE_NT_HEADERS64 nt{};
        if (!ReadMemory(ntAddress, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.OptionalHeader.SizeOfImage == 0 ||
            base > UINTPTR_MAX - nt.OptionalHeader.SizeOfImage) {
            return std::nullopt;
        }
        return ExecutableImage{ base, nt.OptionalHeader.SizeOfImage };
    }

    std::string HexBytes(std::span<const std::uint8_t> a_bytes)
    {
        std::string result;
        for (const auto byte : a_bytes)
            result += std::format("{}{:02X}", result.empty() ? "" : " ", byte);
        return result;
    }
}
