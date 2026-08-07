#pragma once

#include <cstdint>
#include <optional>

namespace RE
{
    class TESObjectREFR;
}

namespace CFS::RuntimeBindings
{
    // Resolves and fingerprints the native callables owned by this module. No
    // callable is published unless the complete required set passes.
    [[nodiscard]] bool Initialize();

    [[nodiscard]] bool IsShipInSpace(RE::TESObjectREFR* a_ship);
    [[nodiscard]] bool SetShipHudTarget(std::uint32_t a_formID);
    [[nodiscard]] std::optional<std::uint32_t> CurrentShipHudTarget();
    [[nodiscard]] bool SelectGalaxySystem(void* a_galaxyState,
        std::uint32_t a_systemBodyID);
    [[nodiscard]] bool CloseGalaxyQuickSelect(void* a_galaxyState,
        void* a_dataModel);

    [[nodiscard]] std::uintptr_t StarMapMenuVtable();
    [[nodiscard]] std::uintptr_t GalaxyStateVtable();
}
