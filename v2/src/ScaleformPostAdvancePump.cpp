#include "ScaleformPostAdvancePump.h"

#include "REL/ID.h"
#include "RE/IDs.h"
#include "REL/ASM.h"
#include "REL/Pattern.h"
#include "REL/Relocation.h"
#include "REL/Trampoline.h"
#include "REX/REX.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace CFS::ScaleformPostAdvancePump
{
    namespace
    {
        // Bytes stolen from the function entry; must cover the 5-byte jmp written by write_jmp<5> and end on an instruction boundary.
        constexpr std::size_t kPrologueSize{5};

        using AdvanceFn = void *(*)(void *, void *, void *, void *);

        std::atomic<AdvanceFn> g_original{nullptr};
        std::atomic<PostAdvanceCallback> g_callback{nullptr};
        std::mutex g_installMutex;
        bool g_installed{false};
        bool g_installTried{false};

        void *AdvanceThunk(void *a_1, void *a_2, void *a_3, void *a_4)
        {
            const auto original = g_original.load(std::memory_order_acquire);
            void *result = original ? original(a_1, a_2, a_3, a_4) : nullptr;

            if (const auto callback = g_callback.load(std::memory_order_acquire)) {
                callback();
            }
            return result;
        }
    }

    bool Install(PostAdvanceCallback a_callback)
    {
        if (!a_callback)
        {
            REX::ERROR("ScaleformPostAdvancePump: refused null post-advance callback");
            return false;
        }

        std::scoped_lock lock{g_installMutex};
        const auto owner = g_callback.load(std::memory_order_acquire);
        if (owner && owner != a_callback)
        {
            REX::ERROR("ScaleformPostAdvancePump: refused a second post-advance callback owner");
            return false;
        }
        if (g_installTried)
        {
            return g_installed;
        }

        g_callback.store(a_callback, std::memory_order_release);
        g_installTried = true;

        REL::Relocation<std::uintptr_t> advance{RE::ID::UI::AdvanceActiveMenus};
        const auto address = advance.address();
        if (!address)
        {
            REX::ERROR("ScaleformPostAdvancePump: failed to resolve UI_AdvanceActiveMenus address;");
            return false;
        }

        auto &trampoline = REL::GetTrampoline();
        constexpr auto gatewayBytes = kPrologueSize + sizeof(REL::ASM::JMP14);
        constexpr auto branchIslandBytes = sizeof(REL::ASM::JMP14);

        auto *gateway = static_cast<std::byte *>(trampoline.allocate(gatewayBytes));
        std::memcpy(gateway, reinterpret_cast<const void *>(address), kPrologueSize);
        const REL::ASM::JMP14 jumpBack{address + kPrologueSize};
        std::memcpy(gateway + kPrologueSize, &jumpBack, sizeof(jumpBack));

        g_original.store(reinterpret_cast<AdvanceFn>(gateway), std::memory_order_release);
        advance.write_jmp<5>(&AdvanceThunk);
        g_installed = true;
        REX::INFO("ScaleformPostAdvancePump: armed at UI_AdvanceActiveMenus entry; Scaleform work runs post-advance on the owning game thread");
        return true;
    }
}
