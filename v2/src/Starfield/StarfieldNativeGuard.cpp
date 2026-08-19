#include "Starfield/StarfieldNativeGuard.h"

#include <Windows.h>
#undef ERROR

#include <cstring>
#include <limits>

namespace CFS::V2::Native
{
    bool ExecutableImage::Contains(std::uintptr_t address, std::size_t length) const
    {
        return base != 0 && address >= base && length <= size && address - base <= size - length;
    }

    ExecutableImage CurrentExecutable()
    {
        const auto module = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
        if (!module) {
            return {};
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
            return {};
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + static_cast<std::uintptr_t>(dos->e_lfanew));
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            return {};
        }
        return {.base = module, .size = nt->OptionalHeader.SizeOfImage};
    }

    bool IsReadable(std::uintptr_t address, std::size_t length)
    {
        if (!address || !length || address > (std::numeric_limits<std::uintptr_t>::max)() - length) {
            return false;
        }

        std::uintptr_t cursor = address;
        const auto end = address + length;
        while (cursor < end) {
            MEMORY_BASIC_INFORMATION info {};
            if (::VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
                return false;
            }
            const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            if (regionEnd <= cursor) {
                return false;
            }
            cursor = regionEnd;
        }
        return true;
    }

    bool IsExecutable(std::uintptr_t address, std::size_t length)
    {
        if (!IsReadable(address, length)) {
            return false;
        }
        MEMORY_BASIC_INFORMATION info {};
        if (::VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info)) {
            return false;
        }
        const auto protection = info.Protect & 0xFF;
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    }

    bool Matches(std::uintptr_t address, std::span<const std::uint8_t> expected)
    {
        const auto image = CurrentExecutable();
        return image.Contains(address, expected.size()) && IsReadable(address, expected.size()) &&
            std::memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) == 0;
    }

    std::uintptr_t DecodeRelativeCall(std::uintptr_t address)
    {
        if (!IsReadable(address, 5)) {
            return 0;
        }
        std::uint8_t opcode = 0;
        std::int32_t displacement = 0;
        std::memcpy(&opcode, reinterpret_cast<const void*>(address), sizeof(opcode));
        std::memcpy(&displacement, reinterpret_cast<const void*>(address + 1), sizeof(displacement));
        if (opcode != 0xE8) {
            return 0;
        }
        return address + 5 + displacement;
    }
}
