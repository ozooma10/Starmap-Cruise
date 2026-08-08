#include "Engine/GalaxyState.h"

#include "Engine/RuntimeBindings.h"

#include <cstddef>
#include <cstring>
#include <format>

namespace CFS::Engine::GalaxyState
{
    namespace
    {
        // Starfield 1.16.244 layout: StarMapMenu owns its UI data model and
        // the active GalaxyState; GalaxyState publishes the selected system
        // and the Quick Select route-ownership byte that SetRouteDestination
        // consumes, then closes itself. The non-entering selected-system
        // setter is vtable slot +0x48 (Address Library ID 94292, owned by
        // RuntimeBindings).
        constexpr std::size_t kStarMapMenuDataModelOffset = 0x1B8;
        constexpr std::size_t kStarMapMenuGalaxyStateOffset = 0x1240;
        constexpr std::size_t kGalaxyStateSelectedSystemOffset = 0x880;
        constexpr std::size_t kGalaxyStateQuickSelectOpenOffset = 0x8F8;
    }

    bool Resolve(const void* a_menu, Live& a_live, std::string& a_detail)
    {
        a_live.menuAddress = reinterpret_cast<std::uintptr_t>(a_menu);
        const auto expectedMenuVtable = RuntimeBindings::StarMapMenuVtable();
        std::uintptr_t actualMenuVtable = 0;
        std::memcpy(&actualMenuVtable,
            reinterpret_cast<const void*>(a_live.menuAddress),
            sizeof(actualMenuVtable));
        if (!expectedMenuVtable || actualMenuVtable != expectedMenuVtable) {
            a_detail = std::format(
                "StarMapMenu primary vtable mismatch (actual={:016X} expected={:016X})",
                actualMenuVtable, expectedMenuVtable);
            return false;
        }

        std::memcpy(&a_live.state,
            reinterpret_cast<const void*>(a_live.menuAddress +
                kStarMapMenuGalaxyStateOffset),
            sizeof(a_live.state));
        if (!a_live.state) {
            a_detail = "StarMapMenu has no active GalaxyState";
            return false;
        }

        const auto expectedGalaxyVtable = RuntimeBindings::GalaxyStateVtable();
        std::uintptr_t actualGalaxyVtable = 0;
        std::memcpy(&actualGalaxyVtable, a_live.state,
            sizeof(actualGalaxyVtable));
        if (!expectedGalaxyVtable || actualGalaxyVtable != expectedGalaxyVtable) {
            a_detail = std::format(
                "GalaxyState primary vtable mismatch (actual={:016X} expected={:016X})",
                actualGalaxyVtable, expectedGalaxyVtable);
            return false;
        }
        return true;
    }

    void ReadSelection(const Live& a_live, std::uint32_t& a_selectedSystem,
        bool& a_quickSelectOpen)
    {
        const auto galaxyAddress = reinterpret_cast<std::uintptr_t>(a_live.state);
        std::memcpy(&a_selectedSystem,
            reinterpret_cast<const void*>(galaxyAddress +
                kGalaxyStateSelectedSystemOffset),
            sizeof(a_selectedSystem));
        a_quickSelectOpen = ReadQuickSelectOpen(a_live);
    }

    bool ReadQuickSelectOpen(const Live& a_live)
    {
        const auto galaxyAddress = reinterpret_cast<std::uintptr_t>(a_live.state);
        std::uint8_t open = 0;
        std::memcpy(&open,
            reinterpret_cast<const void*>(galaxyAddress +
                kGalaxyStateQuickSelectOpenOffset),
            sizeof(open));
        return open != 0;
    }

    bool ArmQuickSelectRouteOwnership(const Live& a_live,
        std::uint32_t a_systemBodyID, std::string& a_detail)
    {
        const auto galaxyAddress = reinterpret_cast<std::uintptr_t>(a_live.state);
        std::uint32_t selectedSystem = 0;
        std::memcpy(&selectedSystem,
            reinterpret_cast<const void*>(galaxyAddress +
                kGalaxyStateSelectedSystemOffset),
            sizeof(selectedSystem));
        if (selectedSystem != a_systemBodyID) {
            a_detail = std::format(
                "native selected-system changed before Set Course (expected={:08X} actual={:08X})",
                a_systemBodyID, selectedSystem);
            return false;
        }

        const std::uint8_t open = 1;
        std::memcpy(reinterpret_cast<void*>(galaxyAddress +
                kGalaxyStateQuickSelectOpenOffset),
            &open, sizeof(open));
        a_detail = std::format(
            "armed native Quick Select route ownership for selected={:08X}",
            selectedSystem);
        return true;
    }

    bool CloseQuickSelect(const Live& a_live)
    {
        return RuntimeBindings::CloseGalaxyQuickSelect(a_live.state,
            reinterpret_cast<void*>(a_live.menuAddress +
                kStarMapMenuDataModelOffset));
    }
}
