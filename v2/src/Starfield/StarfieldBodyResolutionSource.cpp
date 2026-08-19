#include "Starfield/StarfieldBodyResolutionSource.h"

#include "Starfield/StarfieldNativeGuard.h"

#include "RE/Starfield.h"
#include "REX/REX.h"
#include "SFSE/SFSE.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace
{
    constexpr REL::ID ResolveCurrentBodyId {97914};
    constexpr REL::ID ResolveBodySystemFormId {124608};
    constexpr REL::ID ResolveBodyNumericSystemId {124767};
    constexpr REL::ID ResolveBodyNumericSystemInnerId {124766};
    constexpr REL::ID ResolveBodyBySystemOrdinalId {124772};
    constexpr REL::ID LookupSatelliteRowId {124799};

    constexpr std::size_t NumericOuterInnerCallOffset = 0x6F;
    constexpr std::size_t MaxAncestryDepth = 8;
    constexpr FormID MissingFormId = std::numeric_limits<FormID>::max();

    constexpr std::array<std::uint8_t, 24> AcquiringWrapperPrologue {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30,
    };
    constexpr std::array<std::uint8_t, 16> CurrentBodyPrologue {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B,
        0xD9, 0x48, 0x8B, 0x0D, 0x10, 0x0B, 0x76, 0x04,
    };
    constexpr std::array<std::uint8_t, 16> NumericInnerPrologue {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48,
    };
    constexpr std::array<std::uint8_t, 5> NumericInnerCallBytes {
        0xE8, 0xDC, 0xFE, 0xFF, 0xFF,
    };
    constexpr std::array<std::uint8_t, 24> SatelliteLookupPrologue {
        0x48, 0x83, 0xEC, 0x48, 0x44, 0x0F, 0xB7, 0x05,
        0xF4, 0x86, 0xEA, 0x03, 0x48, 0x8B, 0x09, 0x49,
        0xC1, 0xE0, 0x20, 0x48, 0x81, 0xC1, 0x68, 0x02,
    };
    constexpr std::array<std::uint8_t, 16> ResolveBodyBySystemOrdinalPrologue {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56,
    };

    using ResolveCurrentBodyFunction = FormID* (*)(FormID*);
    using ResolveBodyIdentityFunction = FormID* (*)(FormID*, FormID);
    using ResolveBodyBySystemOrdinalFunction = FormID* (*)(FormID*, FormID, FormID);
    using ResolveNumericInnerFunction = FormID* (*)(FormID*, std::uintptr_t*, FormID);
    using LookupSatelliteFunction = const void* (*)(std::uintptr_t*, FormID);

    struct ComponentCopy
    {
        FormID bodyId {0};
        FormID numericSystemId {0};
        FormID parentOrdinal {0};
        FormID planetOrdinal {0};
        bool satellitePresent {false};
    };

    struct CaptureTls
    {
        bool armed {false};
        bool reentrant {false};
        bool captured {false};
        FormID expectedBodyId {0};
        ComponentCopy copy;
    };

    ResolveCurrentBodyFunction g_resolveCurrentBody {nullptr};
    ResolveBodyIdentityFunction g_resolveSystemForm {nullptr};
    ResolveBodyIdentityFunction g_resolveNumericSystem {nullptr};
    ResolveBodyBySystemOrdinalFunction g_resolveBodyBySystemOrdinal {nullptr};
    ResolveNumericInnerFunction g_originalResolveNumericInner {nullptr};
    LookupSatelliteFunction g_lookupSatellite {nullptr};
    std::atomic_bool g_remotePlanningAvailable {false};
    thread_local CaptureTls g_captureTls;

    bool IsLiveFormType(FormID id, RE::FormType type)
    {
        const auto* form = RE::TESForm::LookupByID(id);
        return form && form->GetFormID() == id && form->GetFormType() == type && !form->IsDeleted();
    }

    FormID* ResolveNumericInnerThunk(FormID* output, std::uintptr_t* databaseContext, FormID bodyId) noexcept
    {
        const bool capture = g_captureTls.armed && !g_captureTls.reentrant &&
            g_captureTls.expectedBodyId == bodyId;
        if (capture) {
            g_captureTls.reentrant = true;
        }

        auto* result = g_originalResolveNumericInner(output, databaseContext, bodyId);
        if (!capture) {
            return result;
        }

        ComponentCopy copy {.bodyId = bodyId};
        if (databaseContext && *databaseContext) {
            const auto* row = static_cast<const std::byte*>(g_lookupSatellite(databaseContext, bodyId));
            if (row && CFS::V2::Native::IsReadable(reinterpret_cast<std::uintptr_t>(row), 12)) {
                std::memcpy(&copy.numericSystemId, row, sizeof(FormID));
                std::memcpy(&copy.parentOrdinal, row + sizeof(FormID), sizeof(FormID));
                std::memcpy(&copy.planetOrdinal, row + sizeof(FormID) * 2, sizeof(FormID));
                copy.satellitePresent = true;
            }
        }

        g_captureTls.copy = copy;
        g_captureTls.captured = true;
        g_captureTls.reentrant = false;
        return result;
    }

    bool ResolveNumeric(FormID bodyId, FormID& numericId, ComponentCopy* component)
    {
        if (component) {
            *component = {};
        }
        if (!g_resolveNumericSystem) {
            static REL::Relocation<ResolveBodyIdentityFunction> fallback {ResolveBodyNumericSystemId};
            g_resolveNumericSystem = fallback.get();
        }
        if (!g_resolveNumericSystem || bodyId == 0) {
            return false;
        }

        if (!g_remotePlanningAvailable.load(std::memory_order_acquire)) {
            numericId = MissingFormId;
            return g_resolveNumericSystem(&numericId, bodyId) == std::addressof(numericId) && numericId != MissingFormId;
        }
        if (g_captureTls.armed) {
            return false;
        }

        g_captureTls = {.armed = true, .expectedBodyId = bodyId};
        numericId = MissingFormId;
        const auto* returned = g_resolveNumericSystem(&numericId, bodyId);
        const bool captured = g_captureTls.captured;
        const auto copy = g_captureTls.copy;
        g_captureTls = {};
        if (captured && component) {
            *component = copy;
        }
        return returned == std::addressof(numericId) && numericId != MissingFormId && captured;
    }

    bool ResolveSystemForm(FormID bodyId, FormID& starFormId)
    {
        if (!g_resolveSystemForm) {
            static REL::Relocation<ResolveBodyIdentityFunction> fallback {ResolveBodySystemFormId};
            g_resolveSystemForm = fallback.get();
        }
        starFormId = MissingFormId;
        return g_resolveSystemForm && g_resolveSystemForm(&starFormId, bodyId) == std::addressof(starFormId) &&
            IsLiveFormType(starFormId, RE::FormType::kSTDT);
    }

    std::optional<RemoteTargetPlan> BuildPlan(const SystemIdentity& system, ComponentCopy child)
    {
        if (!g_remotePlanningAvailable.load(std::memory_order_acquire) || !g_resolveBodyBySystemOrdinal ||
            !child.satellitePresent || child.numericSystemId != system.numericId) {
            return std::nullopt;
        }

        RemoteTargetPlan plan;
        std::array<FormID, MaxAncestryDepth + 1> visited {};
        std::size_t visitedCount = 0;
        visited[visitedCount++] = child.bodyId;

        for (std::size_t depth = 0; depth < MaxAncestryDepth; ++depth) {
            if (child.parentOrdinal == 0) {
                return plan;
            }

            FormID parentId = MissingFormId;
            if (g_resolveBodyBySystemOrdinal(&parentId, system.numericId, child.parentOrdinal) != std::addressof(parentId) ||
                parentId == 0 || parentId == MissingFormId ||
                !IsLiveFormType(parentId, RE::FormType::kPNDT) ||
                std::find(visited.begin(), visited.begin() + visitedCount, parentId) != visited.begin() + visitedCount) {
                return std::nullopt;
            }

            FormID parentStar = MissingFormId;
            FormID parentNumeric = MissingFormId;
            ComponentCopy parent;
            if (!ResolveSystemForm(parentId, parentStar) || parentStar != system.starFormId ||
                !ResolveNumeric(parentId, parentNumeric, &parent) || !parent.satellitePresent ||
                parent.bodyId != parentId || parent.numericSystemId != system.numericId ||
                parentNumeric != system.numericId || parent.planetOrdinal != child.parentOrdinal) {
                return std::nullopt;
            }

            plan.allowedWaypointIds.push_back(parentId);
            visited[visitedCount++] = parentId;
            child = parent;
        }
        return std::nullopt;
    }
}

bool StarfieldBodyResolutionSource::InitializeRemotePlanning()
{
    if (g_remotePlanningAvailable.load(std::memory_order_acquire)) {
        return true;
    }

    const auto currentAddress = ResolveCurrentBodyId.address();
    const auto systemAddress = ResolveBodySystemFormId.address();
    const auto numericAddress = ResolveBodyNumericSystemId.address();
    const auto innerAddress = ResolveBodyNumericSystemInnerId.address();
    const auto reverseAddress = ResolveBodyBySystemOrdinalId.address();
    const auto satelliteAddress = LookupSatelliteRowId.address();
    const auto callSite = numericAddress + NumericOuterInnerCallOffset;

    if (!CFS::V2::Native::Matches(currentAddress, CurrentBodyPrologue) ||
        !CFS::V2::Native::Matches(systemAddress, AcquiringWrapperPrologue) ||
        !CFS::V2::Native::Matches(numericAddress, AcquiringWrapperPrologue) ||
        !CFS::V2::Native::Matches(innerAddress, NumericInnerPrologue) ||
        !CFS::V2::Native::Matches(reverseAddress, ResolveBodyBySystemOrdinalPrologue) ||
        !CFS::V2::Native::Matches(satelliteAddress, SatelliteLookupPrologue) ||
        !CFS::V2::Native::Matches(callSite, NumericInnerCallBytes) ||
        !CFS::V2::Native::IsExecutable(callSite, NumericInnerCallBytes.size()) ||
        CFS::V2::Native::DecodeRelativeCall(callSite) != innerAddress) {
        REX::ERROR("StarfieldBodyResolutionSource: 1.16.244 planetary identity fingerprint failed; remote routing disabled");
        return false;
    }

    g_resolveCurrentBody = reinterpret_cast<ResolveCurrentBodyFunction>(currentAddress);
    g_resolveSystemForm = reinterpret_cast<ResolveBodyIdentityFunction>(systemAddress);
    g_resolveNumericSystem = reinterpret_cast<ResolveBodyIdentityFunction>(numericAddress);
    g_resolveBodyBySystemOrdinal = reinterpret_cast<ResolveBodyBySystemOrdinalFunction>(reverseAddress);
    g_originalResolveNumericInner = reinterpret_cast<ResolveNumericInnerFunction>(innerAddress);
    g_lookupSatellite = reinterpret_cast<LookupSatelliteFunction>(satelliteAddress);

    const auto replaced = REL::GetTrampoline().write_call<5>(callSite, &ResolveNumericInnerThunk);
    g_originalResolveNumericInner = reinterpret_cast<ResolveNumericInnerFunction>(replaced);
    g_remotePlanningAvailable.store(true, std::memory_order_release);
    REX::INFO("StarfieldBodyResolutionSource: guarded planet/moon identity bindings enabled (STDT=124608 numeric=124767 Satellite=124799 parent=124772)");
    return true;
}

bool StarfieldBodyResolutionSource::RemotePlanningAvailable() const
{
    return g_remotePlanningAvailable.load(std::memory_order_acquire);
}

std::optional<ResolvedBody> StarfieldBodyResolutionSource::ResolveBody(FormID bodyId) const
{
    if (!IsLiveFormType(bodyId, RE::FormType::kPNDT)) {
        return std::nullopt;
    }

    FormID starFormId = MissingFormId;
    FormID numericId = MissingFormId;
    ComponentCopy component;
    if (!ResolveSystemForm(bodyId, starFormId) || !ResolveNumeric(bodyId, numericId, &component)) {
        return std::nullopt;
    }

    const SystemIdentity system {.starFormId = starFormId, .numericId = numericId};
    std::optional<RemoteTargetPlan> plan;
    if (component.satellitePresent && component.bodyId == bodyId && component.numericSystemId == numericId) {
        plan = BuildPlan(system, component);
    }

    return ResolvedBody {
        .id = bodyId,
        .system = system,
        .remotePlan = std::move(plan),
    };
}

std::optional<SystemIdentity> StarfieldBodyResolutionSource::ResolveSystemIdentity(FormID formId) const
{
    const auto* form = RE::TESForm::LookupByID(formId);
    if (!form || form->GetFormID() != formId || form->IsDeleted()) {
        return std::nullopt;
    }

    SystemIdentity identity;
    const auto type = form->GetFormType();
    if (type == RE::FormType::kSTDT) {
        identity.starFormId = formId;
    } else if (type == RE::FormType::kPNDT) {
        if (!ResolveSystemForm(formId, identity.starFormId)) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    ComponentCopy component;
    if (!ResolveNumeric(formId, identity.numericId, &component) || !identity.IsValid()) {
        return std::nullopt;
    }
    if (type == RE::FormType::kPNDT &&
        (!component.satellitePresent || component.bodyId != formId || component.numericSystemId != identity.numericId)) {
        return std::nullopt;
    }

    const auto* post = RE::TESForm::LookupByID(formId);
    if (!post || post->GetFormID() != formId || post->GetFormType() != type || post->IsDeleted()) {
        return std::nullopt;
    }
    return identity;
}

std::optional<SystemIdentity> StarfieldBodyResolutionSource::ResolveCurrentSystem() const
{
    if (!g_resolveCurrentBody) {
        static REL::Relocation<ResolveCurrentBodyFunction> fallback {ResolveCurrentBodyId};
        g_resolveCurrentBody = fallback.get();
    }
    FormID bodyId = MissingFormId;
    if (!g_resolveCurrentBody || g_resolveCurrentBody(&bodyId) != std::addressof(bodyId)) {
        return std::nullopt;
    }
    return ResolveSystemIdentity(bodyId);
}
