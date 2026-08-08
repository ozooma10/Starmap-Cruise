#pragma once

#include <cstdint>
#include <string>

namespace CFS::Engine::GalaxyState
{
    // A vtable-proven live StarMapMenu/GalaxyState pair. Resolve() proves both
    // primary vtables immediately before every native touch; the pair is valid
    // only for the single post-advance pass that resolved it and must never be
    // retained.
    struct Live
    {
        std::uintptr_t menuAddress{ 0 };
        void* state{ nullptr };
    };

    [[nodiscard]] bool Resolve(const void* a_menu, Live& a_live,
        std::string& a_detail);
    void ReadSelection(const Live& a_live, std::uint32_t& a_selectedSystem,
        bool& a_quickSelectOpen);
    [[nodiscard]] bool ReadQuickSelectOpen(const Live& a_live);
    // Re-checks the selected system and arms the Quick Select route-ownership
    // byte in one call so the check-write pair stays together.
    [[nodiscard]] bool ArmQuickSelectRouteOwnership(const Live& a_live,
        std::uint32_t a_systemBodyID, std::string& a_detail);
    [[nodiscard]] bool CloseQuickSelect(const Live& a_live);
}
