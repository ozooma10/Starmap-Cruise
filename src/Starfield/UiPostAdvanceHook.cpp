#include "Starfield/UiPostAdvanceHook.h"

#include "REL/ID.h"
#include "RE/IDs.h"
#include "REL/ASM.h"
#include "REL/Relocation.h"
#include "REL/Trampoline.h"
#include "REX/REX.h"

#include <array>
#include <cstdint>

namespace CFS::UiPostAdvanceHook
{
    namespace
    {
        constexpr std::size_t kCallSiteOffsets[]{0x228, 0x2A1};

        using AdvanceFn = void *(*)(void *, void *, void *, void *);

        std::array<AdvanceFn, 2> g_original{};
        PostAdvanceCallback g_callback{nullptr};
        bool g_installed{false};

        template <std::size_t Index>
        void *AdvanceThunk(void *a_1, void *a_2, void *a_3, void *a_4)
        {
            void *result = g_original[Index](a_1, a_2, a_3, a_4);
            g_callback();
            return result;
        }
    }

    bool Install(PostAdvanceCallback a_callback)
    {
        if (g_installed) {
            return true;
        }

        REL::Relocation<std::uintptr_t> updateMenus{RE::ID::UI::UpdateMenus};
        const auto base = updateMenus.address();

        // Each call can already lead through a different mod's trampoline.
        // Capture both predecessors before publishing either of our hooks.
        for (std::size_t index = 0; index < g_original.size(); ++index) {
            const auto site = base + kCallSiteOffsets[index];
            if (*reinterpret_cast<const std::uint8_t *>(site) != 0xE8) {
                REX::ERROR("UiPostAdvanceHook: expected a five-byte call at UI_UpdateMenus+{:X}; post-advance callback disabled", kCallSiteOffsets[index]);
                return false;
            }
            g_original[index] = reinterpret_cast<AdvanceFn>(REL::ASM::CALL5::TARGET(site));
        }

        g_callback = a_callback;
        REL::GetTrampoline().write_call<5>(base + kCallSiteOffsets[0], &AdvanceThunk<0>);
        REL::GetTrampoline().write_call<5>(base + kCallSiteOffsets[1], &AdvanceThunk<1>);
        g_installed = true;
        REX::INFO("UiPostAdvanceHook: armed both UI_AdvanceActiveMenus call sites in UI_UpdateMenus");
        return true;
    }
}
