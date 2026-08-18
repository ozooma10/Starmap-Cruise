#include "Starfield/StationTargetBridge.h"

#include "RE/Starfield.h"
#include "REX/REX.h"

#include <Windows.h>
#undef ERROR

#include <array>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr REL::ID SetShipHudTargetId {97892};
    constexpr REL::ID CurrentShipHudTargetId {883585};
    constexpr FormID IsStarstationKeywordId = 0x003402A3;
    constexpr FormID TheEyeMapId = 0x0001285A;
    constexpr FormID TheEyeTargetId = 0x00012894;
    constexpr FormID AlphaCentauriSystemId = 0x00011720;

    constexpr std::array<std::uint8_t, 6> SetShipHudTargetPrefix {
        0x48, 0x83, 0xEC, 0x48, 0x89, 0x0D,
    };

    bool ContainsRange(std::uintptr_t imageBase, std::uintptr_t imageEnd, std::uintptr_t address, std::size_t size)
    {
        return address >= imageBase && address < imageEnd && size <= imageEnd - address;
    }
}

bool StationTargetBridge::Initialize()
{
    static_assert(SetShipHudTargetId.id() == 97892);
    static_assert(CurrentShipHudTargetId.id() == 883585);

    m_setTarget = nullptr;
    m_currentTarget = nullptr;

    REL::Relocation<SetTargetFunction> setter {SetShipHudTargetId};
    REL::Relocation<FormID*> current {CurrentShipHudTargetId};
    const auto setterAddress = setter.address();
    const auto currentAddress = current.address();
    const auto imageBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
    if (!imageBase) {
        REX::ERROR("StationTargetBridge: could not inspect the executable image");
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        REX::ERROR("StationTargetBridge: found an invalid DOS header");
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        REX::ERROR("StationTargetBridge: found an invalid NT header");
        return false;
    }

    const auto imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;
    constexpr std::size_t fingerprintSize = 12;
    if (!ContainsRange(imageBase, imageEnd, setterAddress, fingerprintSize) ||
        !ContainsRange(imageBase, imageEnd, currentAddress, sizeof(FormID))) {
        REX::ERROR("StationTargetBridge: relocation outside executable image; image=[0x{:X}, 0x{:X}) setter=0x{:X} current=0x{:X}", imageBase, imageEnd, setterAddress, currentAddress);
        return false;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(setterAddress);
    if (std::memcmp(bytes, SetShipHudTargetPrefix.data(), SetShipHudTargetPrefix.size()) != 0 ||
        bytes[10] != 0x85 || bytes[11] != 0xC9) {
        REX::ERROR( "StationTargetBridge: fingerprint mismatch at RVA=0x{:X}; observed={:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}", setterAddress - imageBase, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11]);
        return false;
    }

    m_setTarget = setter.get();
    m_currentTarget = current.get();
    REX::INFO("StationTargetBridge: validated at RVA=0x{:X}", setterAddress - imageBase);
    return true;
}

void StationTargetBridge::Resolve(TargetObservation& target) const
{
    if (!m_setTarget ||
        target.kind != ObservedTargetKind::Station ||
        target.id != TheEyeMapId) {
        return;
    }

    target.resolvedTargetId = TheEyeTargetId;
    target.resolvedSystemId = AlphaCentauriSystemId;
}

bool StationTargetBridge::Assign(FormID targetId) const
{
    const auto reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(targetId);
    const auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(IsStarstationKeywordId);
    const auto base = reference ? reference->GetBaseObject() : nullptr;
    if (!m_setTarget || !m_currentTarget || !reference || reference->IsDeleted() || !base || base->IsDeleted() || !keyword || !reference->HasKeyword(keyword)) {
        REX::ERROR("StationTargetBridge: refusing target {:08X}; live REFR/keyword validation failed", targetId);
        return false;
    }

    m_setTarget(targetId);

    const auto observed = *m_currentTarget;
    if (observed != targetId) {
        REX::ERROR("StationTargetBridge: target {:08X} did not commit; observed {:08X}", targetId, observed);
        return false;
    }

    REX::INFO("StationTargetBridge: assigned target {:08X}", targetId);
    return true;
}
