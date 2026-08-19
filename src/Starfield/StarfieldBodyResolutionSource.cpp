#include "Starfield/StarfieldBodyResolutionSource.h"

#include "RE/Starfield.h"
#include "REL/ASM.h"
#include "REL/Pattern.h"
#include "REX/REX.h"
#include "SFSE/SFSE.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
    constexpr REL::ID ResolveBodyNumericSystemInnerId {124766};
    constexpr REL::ID LookupSatelliteRowId {124799};

    constexpr std::size_t NumericOuterInnerCallOffset = 0x6F;
    constexpr std::size_t MaxAncestryDepth = 8;
    constexpr FormID MissingFormId = std::numeric_limits<FormID>::max();

    constexpr auto AcquiringWrapperPattern = REL::Pattern<"48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 30">();
    constexpr auto CurrentBodyPattern = REL::Pattern<"40 53 48 83 EC 40 48 8B D9 48 8B 0D 10 0B 76 04">();
    constexpr auto NumericInnerPattern = REL::Pattern<"48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 40 48">();
    constexpr auto NumericInnerCallPattern = REL::Pattern<"E8 DC FE FF FF">();
    constexpr auto SatelliteLookupPattern = REL::Pattern<"48 83 EC 48 44 0F B7 05 F4 86 EA 03 48 8B 09 49 C1 E0 20 48 81 C1 68 02">();
    constexpr auto ResolveBodyBySystemOrdinalPattern = REL::Pattern<"48 89 5C 24 10 48 89 6C 24 18 56 57 41 55 41 56">();

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
        const bool capture = g_captureTls.armed && !g_captureTls.reentrant && g_captureTls.expectedBodyId == bodyId;
        if (capture) {
            g_captureTls.reentrant = true;
        }

        auto* result = g_originalResolveNumericInner(output, databaseContext, bodyId);
        if (!capture) {
            return result;
        }

        ComponentCopy copy {.bodyId = bodyId};
        if (databaseContext && *databaseContext) {
            const auto* row = g_lookupSatellite(databaseContext, bodyId);
            if (row) {
                RE::BSGalaxy::SatelliteCSVData satellite {};
                std::memcpy(&satellite, row, sizeof(satellite));
                copy.numericSystemId = satellite.numericSystemID;
                copy.parentOrdinal = satellite.parentOrdinal;
                copy.planetOrdinal = satellite.planetOrdinal;
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
        if (bodyId == 0) {
            return false;
        }

        if (!g_remotePlanningAvailable.load(std::memory_order_acquire)) {
            const auto resolved = RE::BSGalaxy::GetNumericSystemID(bodyId);
            if (!resolved) {
                return false;
            }
            numericId = *resolved;
            return true;
        }
        if (g_captureTls.armed) {
            return false;
        }

        g_captureTls = {.armed = true, .expectedBodyId = bodyId};
        const auto resolved = RE::BSGalaxy::GetNumericSystemID(bodyId);
        const bool captured = g_captureTls.captured;
        const auto copy = g_captureTls.copy;
        g_captureTls = {};
        if (!resolved || !captured) {
            return false;
        }
        numericId = *resolved;
        if (component) {
            *component = copy;
        }
        return true;
    }

    bool ResolveSystemForm(FormID bodyId, FormID& starFormId)
    {
        const auto resolved = RE::BSGalaxy::GetSystemFormID(bodyId);
        if (!resolved || !IsLiveFormType(*resolved, RE::FormType::kSTDT)) {
            return false;
        }
        starFormId = *resolved;
        return true;
    }

    std::optional<RemoteTargetPlan> BuildPlan(const SystemIdentity& system, ComponentCopy child)
    {
        if (!g_remotePlanningAvailable.load(std::memory_order_acquire) || !child.satellitePresent || child.numericSystemId != system.numericId) {
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

            const auto resolvedParentId = RE::BSGalaxy::GetBodyFormID(system.numericId, child.parentOrdinal);
            if (!resolvedParentId || !IsLiveFormType(*resolvedParentId, RE::FormType::kPNDT) || std::find(visited.begin(), visited.begin() + visitedCount, *resolvedParentId) != visited.begin() + visitedCount) {
                return std::nullopt;
            }
            const FormID parentId = *resolvedParentId;

            FormID parentStar = MissingFormId;
            FormID parentNumeric = MissingFormId;
            ComponentCopy parent;
            if (!ResolveSystemForm(parentId, parentStar) || parentStar != system.starFormId || !ResolveNumeric(parentId, parentNumeric, &parent) || !parent.satellitePresent || parent.bodyId != parentId ||
                parent.numericSystemId != system.numericId || parentNumeric != system.numericId || parent.planetOrdinal != child.parentOrdinal) {
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

    const auto currentAddress = RE::ID::BGSPlanet::GetCurrentBodyFormID.address();
    const auto systemAddress = RE::ID::BSGalaxy::GetSystemFormID.address();
    const auto numericAddress = RE::ID::BSGalaxy::GetNumericSystemID.address();
    const auto innerAddress = ResolveBodyNumericSystemInnerId.address();
    const auto reverseAddress = RE::ID::BSGalaxy::GetBodyFormID.address();
    const auto satelliteAddress = LookupSatelliteRowId.address();
    const auto callSite = numericAddress + NumericOuterInnerCallOffset;

    const bool currentMatches = CurrentBodyPattern.match(currentAddress);
    const bool systemMatches = AcquiringWrapperPattern.match(systemAddress);
    const bool numericMatches = AcquiringWrapperPattern.match(numericAddress);
    const bool innerMatches = NumericInnerPattern.match(innerAddress);
    const bool reverseMatches = ResolveBodyBySystemOrdinalPattern.match(reverseAddress);
    const bool satelliteMatches = SatelliteLookupPattern.match(satelliteAddress);
    const bool callMatches = NumericInnerCallPattern.match(callSite);
    const bool callTargetMatches = callMatches && REL::ASM::CALL5::TARGET(callSite) == innerAddress;

    if (!currentMatches || !systemMatches || !numericMatches || !innerMatches || !reverseMatches || !satelliteMatches || !callMatches || !callTargetMatches) {
        REX::ERROR(
            "StarfieldBodyResolutionSource: 1.16.244 planetary identity fingerprint failed "
            "(current={} system={} numeric={} inner={} reverse={} satellite={} call={} callTarget={}); "
            "remote routing disabled",
            currentMatches,
            systemMatches,
            numericMatches,
            innerMatches,
            reverseMatches,
            satelliteMatches,
            callMatches,
            callTargetMatches
        );
        return false;
    }

    g_originalResolveNumericInner = reinterpret_cast<ResolveNumericInnerFunction>(innerAddress);
    g_lookupSatellite = reinterpret_cast<LookupSatelliteFunction>(satelliteAddress);

    const auto replaced = REL::GetTrampoline().write_call<5>(callSite, &ResolveNumericInnerThunk);
    g_originalResolveNumericInner = reinterpret_cast<ResolveNumericInnerFunction>(replaced);
    g_remotePlanningAvailable.store(true, std::memory_order_release);
    REX::INFO("StarfieldBodyResolutionSource: guarded planet/moon identity bindings enabled (STDT=124608 numeric=124767 Satellite=124799 parent=124772)");
    return true;
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
    if (type == RE::FormType::kPNDT && (!component.satellitePresent || component.bodyId != formId || component.numericSystemId != identity.numericId)) {
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
    const auto bodyId = RE::BGSPlanet::GetCurrentBodyFormID();
    if (!bodyId) {
        return std::nullopt;
    }
    return ResolveSystemIdentity(*bodyId);
}
