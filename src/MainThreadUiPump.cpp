#include "MainThreadUiPump.h"

#include "Engine/RuntimeMemory.h"

#include "REL/ASM.h"
#include "REL/Relocation.h"
#include "REL/Trampoline.h"
#include "REX/REX.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace CFS::MainThreadUiPump
{
    namespace
    {
        // Starfield 1.16.244: UI_AdvanceActiveMenus, VA 0x142542320.
        // Hooking the function entry coexists with OSF UI, which patches its
        // two direct callers and then calls this original entry.
        constexpr REL::ID kAdvanceActiveMenus{130455};
        constexpr std::array<std::uint8_t, 5> kExpectedPrologue{
            0x4C,
            0x89,
            0x44,
            0x24,
            0x18, // mov [rsp+18h], r8
        };

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

            // The engine has completed this active-menu advance. No SFSE task
            // callback reaches Scaleform; all CFS AS3 work enters here.
            if (const auto callback = g_callback.load(std::memory_order_acquire))
                callback();
            return result;
        }
    }

    bool Install(PostAdvanceCallback a_callback)
    {
        if (!a_callback)
        {
            REX::ERROR("MainThreadUiPump: refused null post-advance callback");
            return false;
        }

        std::scoped_lock lock{g_installMutex};
        const auto owner = g_callback.load(std::memory_order_acquire);
        if (owner && owner != a_callback)
        {
            REX::ERROR("MainThreadUiPump: refused a second post-advance callback owner");
            return false;
        }
        if (g_installTried)
        {
            return g_installed;
        }

        g_callback.store(a_callback, std::memory_order_release);
        g_installTried = true;

        REL::Relocation<std::uintptr_t> advance{kAdvanceActiveMenus};
        const auto address = advance.address();
        std::array<std::uint8_t, kExpectedPrologue.size()> observed{};
        if (address)
            std::memcpy(observed.data(), reinterpret_cast<const void *>(address),
                        observed.size());
        if (!address || observed != kExpectedPrologue)
        {
            // Log what was actually there: a moved function on a new build and
            // another plugin's entry patch need different responses.
            const auto image = Engine::CurrentExecutable();
            REX::ERROR("MainThreadUiPump: UI_AdvanceActiveMenus prologue mismatch at "
                       "{:016X} (RVA=0x{:X}): observed=[{}] expected=[{}]; "
                       "Scaleform bridge disabled before installing an unsafe hook",
                       address, address && image ? image->Rva(address) : 0,
                       address ? Engine::HexBytes(observed) : std::string{"unresolved"},
                       Engine::HexBytes(kExpectedPrologue));
            return false;
        }

        auto &trampoline = REL::GetTrampoline();
        constexpr auto gatewayBytes = kExpectedPrologue.size() + sizeof(REL::ASM::JMP14);
        constexpr auto branchIslandBytes = sizeof(REL::ASM::JMP14);
        if (trampoline.empty() || trampoline.free_size() < gatewayBytes + branchIslandBytes)
        {
            REX::ERROR("MainThreadUiPump: trampoline unavailable (free={} need={}); "
                       "Scaleform bridge disabled",
                       trampoline.empty() ? 0 : trampoline.free_size(),
                       gatewayBytes + branchIslandBytes);
            return false;
        }

        auto *gateway = static_cast<std::byte *>(trampoline.allocate(gatewayBytes));
        std::memcpy(gateway, reinterpret_cast<const void *>(address), kExpectedPrologue.size());
        const REL::ASM::JMP14 jumpBack{address + kExpectedPrologue.size()};
        std::memcpy(gateway + kExpectedPrologue.size(), &jumpBack, sizeof(jumpBack));

        g_original.store(reinterpret_cast<AdvanceFn>(gateway), std::memory_order_release);
        advance.write_jmp<5>(&AdvanceThunk);
        g_installed = true;
        REX::INFO("MainThreadUiPump: armed at UI_AdvanceActiveMenus entry; "
                  "Scaleform work runs post-advance on the owning game thread");
        return true;
    }
}
