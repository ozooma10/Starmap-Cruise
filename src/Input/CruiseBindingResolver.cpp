#include "Input/CruiseBindingResolver.h"

#include "Engine/RuntimeMemory.h"

#include "RE/Starfield.h"

#include <array>
#include <cstring>
#include <string>

namespace CFS::Input
{
    namespace
    {
        constexpr REL::ID kControlMapSingletonPtr{ 938003 };
        constexpr std::size_t kControlMapSize = 0x3A0;
        constexpr std::size_t kContextSlotsOffset = 0x10;
        constexpr std::size_t kMappingStride = 0x28;
        constexpr std::size_t kMaxMappings = 4096;
        constexpr std::array<std::uint8_t, 2> kCruiseContexts{ 0x21, 0x4D };
        constexpr const char* kCruiseEvent = "Cruise";
        constexpr const char* kGamepadCruiseEvent = "SHMonocle";

        struct ControlMapArray
        {
            std::uint32_t size;
            std::uint32_t capacity;
            std::uintptr_t data;
        };
        static_assert(sizeof(ControlMapArray) == 0x10);

        struct ControlMapMapping
        {
            std::uintptr_t eventEntry;
            std::uint32_t keyCode;
            std::uint32_t modifierCode;
            std::uint8_t bindingSlot;
            std::uint8_t unk11;
            std::uint16_t unk12;
            std::uint8_t sortIndex;
            std::uint8_t unk15[3];
            std::uint32_t contextMask;
            std::uint8_t bindingMeta;
            std::uint8_t visibleInControls;
            std::uint8_t defaultWasUnbound;
            std::uint8_t unk1F;
            std::uint8_t required;
            std::uint8_t pad21[7];
        };
        static_assert(sizeof(ControlMapMapping) == kMappingStride);

        std::string ReadEventName(std::uintptr_t a_entry)
        {
            for (std::size_t depth = 0; a_entry && depth < 8; ++depth) {
                std::uint8_t flags = 0;
                if (!Engine::ReadMemory(a_entry + 0x14, flags))
                    return {};
                if ((flags & 0x02) == 0) {
                    std::uint32_t length = 0;
                    if (!Engine::ReadMemory(a_entry + 0x08, length) || length > 128 ||
                        !Engine::IsReadableRange(a_entry + 0x18, length)) {
                        return {};
                    }
                    return { reinterpret_cast<const char*>(a_entry + 0x18), length };
                }
                if (!Engine::ReadMemory(a_entry + 0x08, a_entry))
                    return {};
            }
            return {};
        }

        bool FindBinding(std::uintptr_t a_controlMap, std::uint8_t a_context,
            std::uint32_t a_deviceIndex, const char* a_event, Binding& a_binding)
        {
            if (a_deviceIndex > 2)
                return false;

            std::uintptr_t context = 0;
            if (!Engine::ReadMemory(a_controlMap + kContextSlotsOffset +
                    static_cast<std::size_t>(a_context) * sizeof(std::uintptr_t), context) ||
                !context) {
                return false;
            }

            ControlMapArray mappings{};
            if (!Engine::ReadMemory(context +
                    static_cast<std::size_t>(a_deviceIndex) * sizeof(ControlMapArray),
                    mappings) ||
                mappings.size > mappings.capacity || mappings.size > kMaxMappings ||
                (mappings.size && !Engine::IsReadableRange(mappings.data,
                    static_cast<std::size_t>(mappings.size) * kMappingStride))) {
                return false;
            }

            for (const auto desiredSlot : { std::uint8_t{ 0 }, std::uint8_t{ 1 } }) {
                for (std::uint32_t index = 0; index < mappings.size; ++index) {
                    ControlMapMapping mapping{};
                    std::memcpy(&mapping,
                        reinterpret_cast<const void*>(mappings.data +
                            static_cast<std::size_t>(index) * kMappingStride),
                        sizeof(mapping));
                    if (mapping.bindingSlot != desiredSlot ||
                        ReadEventName(mapping.eventEntry) != a_event ||
                        mapping.keyCode == 0xFF || mapping.keyCode == 0x7FFFFFFF ||
                        (a_deviceIndex == 0 && mapping.keyCode > 0xFE)) {
                        continue;
                    }

                    const auto modifier = mapping.modifierCode == 0xFF ||
                                                  mapping.modifierCode == 0x7FFFFFFF ?
                                              -1 :
                                              static_cast<std::int32_t>(mapping.modifierCode);
                    if (a_deviceIndex == 0 && modifier > 0xFE)
                        continue;
                    a_binding = {
                        .code = static_cast<std::int32_t>(mapping.keyCode),
                        .modifier = modifier,
                    };
                    return true;
                }
            }
            return false;
        }
    }

    std::optional<CruiseBindings> ResolveCruiseBindings()
    {
        REL::Relocation<std::uintptr_t*> singleton{ kControlMapSingletonPtr };
        std::uintptr_t controlMap = 0;
        std::uintptr_t vtable = 0;
        if (!Engine::ReadMemory(singleton.address(), controlMap) ||
            !Engine::IsReadableRange(controlMap, kControlMapSize) ||
            !Engine::ReadMemory(controlMap, vtable) ||
            vtable != RE::VTABLE::ControlMap[0].address()) {
            return std::nullopt;
        }

        CruiseBindings result;
        for (const auto context : kCruiseContexts) {
            if (FindBinding(controlMap, context, 0, kCruiseEvent, result.keyboard))
                break;
        }
        for (const auto context : kCruiseContexts) {
            if (FindBinding(controlMap, context, 1, kCruiseEvent, result.mouse))
                break;
        }
        for (const auto context : kCruiseContexts) {
            if (FindBinding(controlMap, context, 2, kGamepadCruiseEvent, result.gamepad))
                break;
        }
        return result;
    }
}
