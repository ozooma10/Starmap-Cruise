#include "Starfield/StationTargetBridge.h"

#include "BodyIndex.h"

#include "RE/Starfield.h"
#include "REX/REX.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr FormID kIsStarstationKeywordId = 0x003402A3;

    FormID LiveStationBase(FormID referenceId)
    {
        const auto reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(referenceId);
        const auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(kIsStarstationKeywordId);
        const auto base = reference ? reference->GetBaseObject() : nullptr;
        if (!reference || reference->IsDeleted() || !base || base->IsDeleted() || !keyword || !reference->HasKeyword(keyword)) {
            return 0;
        }

        return base->GetFormID();
    }

    FormID StationSystem(FormID cellId)
    {
        std::vector<FormID> systems;
        for (const auto& orbital : CFS::BodyIndex::StationOrbitals(cellId)) {
            if (orbital.galaxy.system != 0) {
                systems.push_back(orbital.galaxy.system);
            }
        }

        std::ranges::sort(systems);
        systems.erase(std::unique(systems.begin(), systems.end()), systems.end());
        return systems.size() == 1 ? systems.front() : 0;
    }

    struct StationCandidate
    {
        FormID referenceId {0};
        FormID courseId {0};
    };
}

bool StationTargetBridge::Initialize()
{
    CFS::BodyIndex::StartLoad();
    return true;
}

void StationTargetBridge::Resolve(TargetObservation& target) const
{
    if (target.kind != ObservedTargetKind::Station || !CFS::BodyIndex::Ready()) {
        return;
    }

    FormID cellId = target.id;
    std::vector<StationCandidate> candidates;

    const auto direct = RE::TESForm::LookupByID<RE::TESObjectREFR>(target.id);
    const bool directStation = direct && LiveStationBase(target.id) != 0;
    if (directStation && direct->parentCell) {
        cellId = direct->parentCell->GetFormID();
    }

    for (const auto& indexed : CFS::BodyIndex::StationTargets(cellId)) {
        const auto liveBase = LiveStationBase(indexed.referenceFormID);
        const auto course = indexed.courseFormID ? RE::TESForm::LookupByID<RE::TESObjectREFR>(indexed.courseFormID) : nullptr;
        if (liveBase != 0 && liveBase == indexed.baseFormID && (!indexed.courseFormID || (course && !course->IsDeleted()))) {
            candidates.push_back({indexed.referenceFormID, indexed.courseFormID});
        }
    }

    if (directStation) {
        const auto indexed = std::ranges::find(candidates, target.id, &StationCandidate::referenceId);
        if (indexed == candidates.end()) {
            candidates.push_back({target.id, 0});
        }
    }

    std::ranges::sort(candidates, {}, &StationCandidate::referenceId);
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const StationCandidate& left, const StationCandidate& right) {
        return left.referenceId == right.referenceId;
    }), candidates.end());

    const auto systemId = StationSystem(cellId);
    if (candidates.size() != 1 || systemId == 0) {
        return;
    }

    target.resolvedTargetId = candidates.front().referenceId;
    target.resolvedCourseId = candidates.front().courseId;
    target.resolvedSystemId = systemId;
}

bool StationTargetBridge::Assign(FormID targetId) const
{
    const auto reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(targetId);
    const auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(kIsStarstationKeywordId);
    const auto base = reference ? reference->GetBaseObject() : nullptr;
    if (!reference || reference->IsDeleted() || !base || base->IsDeleted() || !keyword || !reference->HasKeyword(keyword)) {
        REX::ERROR("StationTargetBridge: refusing target {:08X}; live REFR/keyword validation failed", targetId);
        return false;
    }

    RE::ShipHudTarget::Set(targetId);

    const auto observed = RE::ShipHudTarget::GetCurrent();
    if (observed != targetId) {
        REX::ERROR("StationTargetBridge: target {:08X} did not commit; observed {:08X}", targetId, observed);
        return false;
    }

    REX::INFO("StationTargetBridge: assigned target {:08X}", targetId);
    return true;
}
