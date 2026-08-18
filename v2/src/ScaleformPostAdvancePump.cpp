#include "ScaleformPostAdvancePump.h"

#include "REL/ID.h"
#include "RE/IDs.h"
#include "REL/Relocation.h"
#include "REL/Trampoline.h"
#include "REX/REX.h"

#include <cstdint>
#include <cstring>

namespace CFS::ScaleformPostAdvancePump
{
    namespace
    {
        constexpr std::size_t kCallSiteOffsets[]{0x228, 0x2A1};

        using AdvanceFn = void *(*)(void *, void *, void *, void *);

        AdvanceFn g_original{nullptr};
        PostAdvanceCallback g_callback{nullptr};
        bool g_installed{false};

        void *AdvanceThunk(void *a_1, void *a_2, void *a_3, void *a_4)
        {
            void *result = g_original(a_1, a_2, a_3, a_4);
            g_callback();
            return result;
        }

        std::uintptr_t DecodeCallTarget(std::uintptr_t a_site)
        {
            if (*reinterpret_cast<const std::uint8_t *>(a_site) != 0xE8) {
                return 0;
            }
            std::int32_t rel{};
            std::memcpy(&rel, reinterpret_cast<const void *>(a_site + 1), sizeof(rel));
            return a_site + 5 + rel;
        }
    }

    bool Install(PostAdvanceCallback a_callback)
    {
        if (g_installed) {
            return true;
        }

        REL::Relocation<std::uintptr_t> updateMenus{RE::ID::UI::UpdateMenus};
        const auto base = updateMenus.address();

        const auto target = DecodeCallTarget(base + kCallSiteOffsets[0]);
        if (!target || target != DecodeCallTarget(base + kCallSiteOffsets[1])) {
            REX::ERROR("ScaleformPostAdvancePump: stale call-site offsets in UI_UpdateMenus; Scaleform bridge disabled");
            return false;
        }

        g_original = reinterpret_cast<AdvanceFn>(target);
        g_callback = a_callback;
        for (const auto offset : kCallSiteOffsets) {
            REL::GetTrampoline().write_call<5>(base + offset, &AdvanceThunk);
        }
        g_installed = true;
        REX::INFO("ScaleformPostAdvancePump: armed both UI_AdvanceActiveMenus call sites in UI_UpdateMenus");
        return true;
    }
}
