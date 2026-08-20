#include "Input/CruiseBindingResolver.h"

#include "RE/Starfield.h"
#include "SFSE/InputMap.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace CFS::Input
{
    namespace
    {
        enum class Device : std::size_t
        {
            Keyboard,
            Mouse,
            Gamepad,
            Count,
        };

        enum class Context : std::size_t
        {
            ShipHud = 0x21,
            ShipHudCruiseMode = 0x4D,
        };

        constexpr std::array kCruiseContexts{
            Context::ShipHud,
            Context::ShipHudCruiseMode,
        };
        constexpr std::uint32_t kShortUnbound = 0xFF;
        constexpr std::uint32_t kLongUnbound = 0x7FFFFFFF;

        struct RawMapping
        {
            RE::BSStringPool::Entry* eventEntry;
            std::uint32_t keyCode;
            std::uint32_t modifierCode;
            std::uint8_t bindingSlot;
            std::byte unused[0x17];
        };
        static_assert(sizeof(RawMapping) == 0x28);

        struct RawMappingArray
        {
            std::uint32_t size;
            std::uint32_t capacity;
            RawMapping* data;
        };
        static_assert(sizeof(RawMappingArray) == 0x10);

        struct RawContext
        {
            std::array<RawMappingArray, static_cast<std::size_t>(Device::Count)> devices;
        };

        struct RawControlMap
        {
            void* vtable;
            std::uint64_t unknown08;
            std::array<RawContext*, static_cast<std::size_t>(Context::ShipHudCruiseMode) + 1> contexts;
        };

        std::string_view EventName(const RawMapping& a_mapping)
        {
            if (!a_mapping.eventEntry) {
                return {};
            }
            return {
                a_mapping.eventEntry->data<char>(),
                a_mapping.eventEntry->length(),
            };
        }

        bool IsUnbound(std::uint32_t a_code)
        {
            return a_code == kShortUnbound || a_code == kLongUnbound;
        }

        Binding FindBinding(RawControlMap* a_controlMap, Device a_device, std::string_view a_event)
        {
            for (const auto contextId : kCruiseContexts) {
                const auto* context = a_controlMap->contexts[static_cast<std::size_t>(contextId)];
                if (!context) {
                    continue;
                }

                const auto& mappings = context->devices[static_cast<std::size_t>(a_device)];
                for (std::uint8_t slot = 0; slot < 2; ++slot) {
                    for (std::uint32_t index = 0; index < mappings.size; ++index) {
                        const auto& mapping = mappings.data[index];
                        if (mapping.bindingSlot != slot || IsUnbound(mapping.keyCode) || EventName(mapping) != a_event) {
                            continue;
                        }

                        // Keyboard mappings are stored as Win32 virtual-key codes, while ButtonEvent::idCode uses DirectInput set-1 scan codes. 
                        // Modifiers remain virtual-key codes because the input hook checks them with GetAsyncKeyState.
                        const auto inputCode = a_device == Device::Keyboard ? SFSE::InputMap::VirtualKeyToKeycode(mapping.keyCode) : mapping.keyCode;
                        if (a_device == Device::Keyboard && inputCode == 0) {
                            continue;
                        }

                        return {
                            .code = static_cast<std::int32_t>(inputCode),
                            .modifier = IsUnbound(mapping.modifierCode) ? -1 : static_cast<std::int32_t>(mapping.modifierCode),
                        };
                    }
                }
            }
            return {};
        }
    }

    CruiseBindings ResolveCruiseBindings()
    {
        REL::Relocation<RawControlMap**> singleton{ RE::ID::FreeCameraInputContext::Manager };
        auto* controlMap = *singleton;

        return {
            .keyboard = FindBinding(controlMap, Device::Keyboard, "Cruise"),
            .mouse = FindBinding(controlMap, Device::Mouse, "Cruise"),
            .gamepad = FindBinding(controlMap, Device::Gamepad, "SHMonocle"),
        };
    }
}
