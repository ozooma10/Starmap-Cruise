#include "Engine/RuntimeBindings.h"

#include "Engine/RuntimeMemory.h"

#include "RE/Starfield.h"
#include "REX/REX.h"

#include <array>
#include <atomic>
#include <cstring>

namespace CFS::RuntimeBindings
{
    namespace
    {
        constexpr REL::ID kSetShipHudTarget{ 97892 };
        constexpr REL::ID kCurrentShipHudTarget{ 883585 };
        constexpr REL::ID kSelectGalaxySystem{ 94292 };
        constexpr REL::ID kCloseGalaxyQuickSelect{ 94308 };
        constexpr REL::ID kStarMapMenuPrimaryVtable{ 446845 };
        constexpr REL::ID kGalaxyStatePrimaryVtable{ 446425 };

        constexpr std::array<std::uint8_t, 16> kIsInSpace116244Prologue{
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57,
            0x48, 0x83, 0xEC, 0x40, 0x40, 0x32, 0xF6, 0x48,
        };
        constexpr std::array<std::uint8_t, 6> kSetShipHudTarget116244Prefix{
            0x48, 0x83, 0xEC, 0x48, 0x89, 0x0D,
        };
        constexpr std::array<std::uint8_t, 16> kSelectGalaxySystem116244Prologue{
            0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74,
            0x24, 0x20, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xA9,
        };
        constexpr std::array<std::uint8_t, 16> kCloseGalaxyQuickSelect116244Prologue{
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
            0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        };

        using IsInSpace_t = bool (*)(RE::TESObjectREFR*, bool);
        using SetShipHudTarget_t = void (*)(std::uint32_t);
        using SelectGalaxySystem_t = void (*)(void*, std::uint32_t, bool);
        using CloseGalaxyQuickSelect_t = void (*)(void*, void*);

        std::atomic<IsInSpace_t> g_isInSpace{ nullptr };
        std::atomic<SetShipHudTarget_t> g_setShipHudTarget{ nullptr };
        std::atomic<SelectGalaxySystem_t> g_selectGalaxySystem{ nullptr };
        std::atomic<CloseGalaxyQuickSelect_t> g_closeGalaxyQuickSelect{ nullptr };
        std::atomic<std::uintptr_t> g_starMapMenuVtable{ 0 };
        std::atomic<std::uintptr_t> g_galaxyStateVtable{ 0 };

        // Image containment is proven by the caller before these reads.
        template <std::size_t N>
        [[nodiscard]] std::array<std::uint8_t, N> ReadBytes(std::uintptr_t a_address)
        {
            std::array<std::uint8_t, N> observed{};
            std::memcpy(observed.data(), reinterpret_cast<const void*>(a_address), N);
            return observed;
        }

        // On mismatch, log what was actually there: it distinguishes a moved
        // function on a new game build from another plugin's entry patch, and
        // is the raw material for updating the pattern.
        template <std::size_t N>
        bool Matches(std::uintptr_t a_address,
            const std::array<std::uint8_t, N>& a_expected, const char* a_tag,
            std::uint64_t a_rva)
        {
            const auto observed = ReadBytes<N>(a_address);
            if (observed == a_expected)
                return true;
            REX::ERROR("[runtime] {} fingerprint mismatch at RVA=0x{:X}: observed=[{}] expected=[{}]",
                a_tag, a_rva, Engine::HexBytes(observed), Engine::HexBytes(a_expected));
            return false;
        }
    }

    bool Initialize()
    {
        static_assert(RE::ID::TESObjectREFR::IsInSpace.id() == 63482);
        static_assert(kSetShipHudTarget.id() == 97892);
        static_assert(kSelectGalaxySystem.id() == 94292);
        static_assert(kCloseGalaxyQuickSelect.id() == 94308);
        static_assert(kStarMapMenuPrimaryVtable.id() == 446845);
        static_assert(kGalaxyStatePrimaryVtable.id() == 446425);

        const auto image = Engine::CurrentExecutable();
        if (!image) {
            REX::ERROR("[runtime] Starfield executable image is invalid; bridge disabled before hooks");
            return false;
        }

        REL::Relocation<std::uintptr_t> isInSpaceTarget{ RE::ID::TESObjectREFR::IsInSpace };
        REL::Relocation<std::uintptr_t> shipTarget{ kSetShipHudTarget };
        REL::Relocation<std::uintptr_t> selectTarget{ kSelectGalaxySystem };
        REL::Relocation<std::uintptr_t> closeTarget{ kCloseGalaxyQuickSelect };
        REL::Relocation<std::uintptr_t> menuVtable{ kStarMapMenuPrimaryVtable };
        REL::Relocation<std::uintptr_t> galaxyVtable{ kGalaxyStatePrimaryVtable };

        const auto isInSpaceAddress = isInSpaceTarget.address();
        const auto shipTargetAddress = shipTarget.address();
        const auto selectAddress = selectTarget.address();
        const auto closeAddress = closeTarget.address();
        const auto menuVtableAddress = menuVtable.address();
        const auto galaxyVtableAddress = galaxyVtable.address();

        if (!image->Contains(isInSpaceAddress, kIsInSpace116244Prologue.size()) ||
            !Matches(isInSpaceAddress, kIsInSpace116244Prologue, "ID 63482 IsInSpace",
                image->Rva(isInSpaceAddress))) {
            REX::ERROR("[space] Address Library ID 63482 failed the Starfield 1.16.244 executable/fingerprint guard at {:016X}; bridge disabled",
                isInSpaceAddress);
            return false;
        }

        constexpr std::size_t kShipTargetFingerprintSpan = 12;
        if (!image->Contains(shipTargetAddress, kShipTargetFingerprintSpan)) {
            REX::ERROR("[target] Address Library ID 97892 resolved outside Starfield.exe: {:016X}; bridge disabled",
                shipTargetAddress);
            return false;
        }
        // Bytes 6-9 are a rel32 displacement that legitimately varies; compare
        // the 6-byte prefix and the fixed test-instruction bytes around it.
        const auto shipObserved = ReadBytes<kShipTargetFingerprintSpan>(shipTargetAddress);
        const bool shipPrefixMatches = std::memcmp(shipObserved.data(),
            kSetShipHudTarget116244Prefix.data(), kSetShipHudTarget116244Prefix.size()) == 0;
        if (!shipPrefixMatches || shipObserved[10] != 0x85 || shipObserved[11] != 0xC9) {
            REX::ERROR("[target] Address Library ID 97892 failed the Starfield 1.16.244 fingerprint at {:016X} (RVA=0x{:X}): observed=[{}] expectedPrefix=[{}] expected [10..11]=85 C9; bridge disabled",
                shipTargetAddress, image->Rva(shipTargetAddress),
                Engine::HexBytes(shipObserved),
                Engine::HexBytes(kSetShipHudTarget116244Prefix));
            return false;
        }

        if (!image->Contains(selectAddress, kSelectGalaxySystem116244Prologue.size()) ||
            !image->Contains(closeAddress, kCloseGalaxyQuickSelect116244Prologue.size()) ||
            !image->Contains(menuVtableAddress, sizeof(std::uintptr_t)) ||
            !image->Contains(galaxyVtableAddress, sizeof(std::uintptr_t))) {
            REX::ERROR("[jump] galaxy selection Address Library bindings resolve outside Starfield.exe; bridge disabled");
            return false;
        }
        if (!Matches(selectAddress, kSelectGalaxySystem116244Prologue,
                "ID 94292 SelectGalaxySystem", image->Rva(selectAddress))) {
            REX::ERROR("[jump] Address Library ID 94292 failed the Starfield 1.16.244 prologue fingerprint at {:016X}; bridge disabled",
                selectAddress);
            return false;
        }
        if (!Matches(closeAddress, kCloseGalaxyQuickSelect116244Prologue,
                "ID 94308 CloseGalaxyQuickSelect", image->Rva(closeAddress))) {
            REX::ERROR("[jump] Address Library ID 94308 failed the Starfield 1.16.244 prologue fingerprint at {:016X}; bridge disabled",
                closeAddress);
            return false;
        }

        // Publish the complete set together only after every guard succeeds.
        g_isInSpace.store(reinterpret_cast<IsInSpace_t>(isInSpaceAddress),
            std::memory_order_release);
        g_setShipHudTarget.store(reinterpret_cast<SetShipHudTarget_t>(shipTargetAddress),
            std::memory_order_release);
        g_selectGalaxySystem.store(reinterpret_cast<SelectGalaxySystem_t>(selectAddress),
            std::memory_order_release);
        g_closeGalaxyQuickSelect.store(reinterpret_cast<CloseGalaxyQuickSelect_t>(closeAddress),
            std::memory_order_release);
        g_starMapMenuVtable.store(menuVtableAddress, std::memory_order_release);
        g_galaxyStateVtable.store(galaxyVtableAddress, std::memory_order_release);

        REX::INFO("[space] IsInSpace(false) binding validated: Address Library ID 63482, RVA=0x{:X}, fingerprint={} bytes",
            image->Rva(isInSpaceAddress), kIsInSpace116244Prologue.size());
        REX::INFO("[target] native ship-target setter validated: Address Library ID 97892, RVA=0x{:X}",
            image->Rva(shipTargetAddress));
        REX::INFO("[jump] native galaxy selection bindings validated: select ID 94292 RVA=0x{:X}, Quick Select close ID 94308 RVA=0x{:X}, menuVtableID=446845, galaxyVtableID=446425",
            image->Rva(selectAddress), image->Rva(closeAddress));
        return true;
    }

    bool IsShipInSpace(RE::TESObjectREFR* a_ship)
    {
        const auto predicate = g_isInSpace.load(std::memory_order_acquire);
        return a_ship && predicate && predicate(a_ship, false);
    }

    bool SetShipHudTarget(std::uint32_t a_formID)
    {
        const auto setter = g_setShipHudTarget.load(std::memory_order_acquire);
        if (!setter)
            return false;
        setter(a_formID);
        return true;
    }

    std::optional<std::uint32_t> CurrentShipHudTarget()
    {
        REL::Relocation<std::uint32_t*> current{ kCurrentShipHudTarget };
        std::uint32_t observed = 0;
        if (!Engine::ReadMemory(current.address(), observed))
            return std::nullopt;
        return observed;
    }

    bool SelectGalaxySystem(void* a_galaxyState, std::uint32_t a_systemBodyID)
    {
        const auto select = g_selectGalaxySystem.load(std::memory_order_acquire);
        if (!select || !a_galaxyState || !a_systemBodyID)
            return false;
        select(a_galaxyState, a_systemBodyID, false);
        return true;
    }

    bool CloseGalaxyQuickSelect(void* a_galaxyState, void* a_dataModel)
    {
        const auto close = g_closeGalaxyQuickSelect.load(std::memory_order_acquire);
        if (!close || !a_galaxyState || !a_dataModel)
            return false;
        close(a_galaxyState, a_dataModel);
        return true;
    }

    std::uintptr_t StarMapMenuVtable()
    {
        return g_starMapMenuVtable.load(std::memory_order_acquire);
    }

    std::uintptr_t GalaxyStateVtable()
    {
        return g_galaxyStateVtable.load(std::memory_order_acquire);
    }
}
