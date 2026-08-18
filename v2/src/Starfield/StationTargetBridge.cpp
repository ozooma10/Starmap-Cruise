#include "Starfield/StationTargetBridge.h"

#include "REX/REX.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <vector>

namespace
{
    constexpr const char* MapMenuName = "GalaxyStarMapMenu";
    constexpr FormID IsStarstationKeywordId = 0x003402A3;
    constexpr REL::ID StarMapMenuPrimaryVtable {446845};
    constexpr REL::ID SystemStatePrimaryVtable {447180};
    constexpr std::size_t StarMapMenuActiveStateOffset = 0x1240;
    constexpr std::size_t SystemStateDisplayedSystemOffset = 0xA10;
    constexpr std::size_t SystemStateSelectedBodyOffset = 0xA1C;
    constexpr std::size_t MaximumStationCellReferences = 4096;

    using Reference = RE::NiPointer<RE::TESObjectREFR>;
    using Cell = RE::NiPointer<RE::TESObjectCELL>;

    bool IsLiveStation(const Reference& reference,
        RE::BGSKeyword* keyword, const RE::TESObjectCELL* cell)
    {
        const auto base = reference ? reference->GetBaseObject() : nullptr;
        return reference && !reference->IsDeleted() && base && !base->IsDeleted() && keyword && reference->parentCell == cell && reference->HasKeyword(keyword);
    }

    bool IsLiveMapMarker(const Reference& reference, const RE::TESObjectCELL* cell)
    {
        if (!reference || reference->IsDeleted() || reference->parentCell != cell) {
            return false;
        }

        const auto extra = reference->extraDataList.get();
        return extra && extra->HasType(RE::ExtraDataType::kMapMarker);
    }

    std::optional<std::vector<Reference>> SnapshotReferences(const Cell& cell)
    {
        if (!cell) {
            return std::nullopt;
        }

        std::size_t expected = 0;
        {
            RE::BSAutoReadLock lock {cell->lock};
            expected = cell->references.size();
        }

        if (expected > MaximumStationCellReferences) {
            return std::nullopt;
        }

        std::vector<Reference> references;
        references.reserve(expected);
        {
            RE::BSAutoReadLock lock {cell->lock};
            if (cell->references.size() != expected) {
                return std::nullopt;
            }

            for (const auto& reference : cell->references) {
                if (reference) {
                    references.push_back(reference);
                }
            }
        }
        return references;
    }

    void SortUnique(std::vector<Reference>& references)
    {
        std::ranges::sort(references, {}, [](const Reference& reference) {
            return reference->GetFormID();
        });
        references.erase(std::unique(references.begin(), references.end(),
            [](const Reference& left, const Reference& right) {
                return left->GetFormID() == right->GetFormID();
            }), references.end());
    }

    std::optional<FormID> ReadDisplayedSystemForm(FormID selectedMapId, FormID selectedCellId)
    {
        const auto ui = RE::UI::GetSingleton();
        const RE::BSFixedString mapName {MapMenuName};
        const auto menu = ui ? ui->GetMenu(mapName) : nullptr;
        if (!ui || !ui->IsMenuOpen(mapName) || !menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
            return std::nullopt;
        }

        const auto menuAddress = reinterpret_cast<std::uintptr_t>(menu.get());
        std::uintptr_t actualMenuVtable = 0;
        std::memcpy(&actualMenuVtable,
            reinterpret_cast<const void*>(menuAddress),
            sizeof(actualMenuVtable));
        static REL::Relocation<std::uintptr_t> expectedMenuVtable {StarMapMenuPrimaryVtable};
        if (actualMenuVtable != expectedMenuVtable.address()) {
            return std::nullopt;
        }

        void* activeState = nullptr;
        std::memcpy(&activeState, reinterpret_cast<const void*>(menuAddress + StarMapMenuActiveStateOffset), sizeof(activeState));
        if (!activeState) {
            return std::nullopt;
        }

        std::uintptr_t actualStateVtable = 0;
        std::memcpy(&actualStateVtable, activeState, sizeof(actualStateVtable));
        static REL::Relocation<std::uintptr_t> expectedStateVtable {SystemStatePrimaryVtable};
        if (actualStateVtable != expectedStateVtable.address()) {
            return std::nullopt;
        }

        const auto stateAddress = reinterpret_cast<std::uintptr_t>(activeState);
        FormID displayedSystem = 0;
        FormID selectedIdentity = 0;
        std::memcpy(&displayedSystem, reinterpret_cast<const void*>(stateAddress + SystemStateDisplayedSystemOffset), sizeof(displayedSystem));
        std::memcpy(&selectedIdentity, reinterpret_cast<const void*>(stateAddress + SystemStateSelectedBodyOffset), sizeof(selectedIdentity));
        if (displayedSystem == 0 || (selectedIdentity != selectedMapId && selectedIdentity != selectedCellId)) {
            return std::nullopt;
        }

        const auto system = RE::TESForm::LookupByID(displayedSystem);
        if (!system || system->IsDeleted() || system->GetFormType() != RE::FormType::kSTDT) {
            return std::nullopt;
        }
        return displayedSystem;
    }
}

void StationTargetBridge::Resolve(TargetObservation& target)
{
    target.resolvedTargetId = 0;
    target.resolvedCourseId = 0;
    target.displayedSystemFormId.reset();
    if (m_assignmentTargetId != 0) {
        return;
    }

    if (target.kind != ObservedTargetKind::Station || target.id == 0) {
        m_resolvedStation.reset();
        m_resolvedCourse.reset();
        m_resolvedMapId = 0;
        m_displayedSystemFormId = 0;
        return;
    }

    if (target.id == m_resolvedMapId && m_resolvedStation && m_resolvedCourse && m_displayedSystemFormId != 0) {
        target.resolvedTargetId = m_resolvedStation->GetFormID();
        target.resolvedCourseId = m_resolvedCourse->GetFormID();
        target.displayedSystemFormId = m_displayedSystemFormId;
        return;
    }

    m_resolvedStation.reset();
    m_resolvedCourse.reset();
    m_resolvedMapId = 0;
    m_displayedSystemFormId = 0;

    const auto started = std::chrono::steady_clock::now();

    const auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(IsStarstationKeywordId);
    if (!keyword) {
        return;
    }

    Cell cell {RE::TESForm::LookupByID<RE::TESObjectCELL>(target.id)};
    Reference directStation;
    if (!cell) {
        directStation.reset(RE::TESForm::LookupByID<RE::TESObjectREFR>(target.id));
        if (!directStation || !directStation->parentCell) {
            return;
        }
        cell.reset(directStation->parentCell);
        if (!IsLiveStation(directStation, keyword, cell.get())) {
            return;
        }
    }

    const auto references = SnapshotReferences(cell);
    if (!references) {
        return;
    }

    std::vector<Reference> stations;
    std::vector<Reference> mapMarkers;
    stations.reserve(references->size() + (directStation ? 1 : 0));
    mapMarkers.reserve(references->size());
    for (const auto& reference : *references) {
        if (IsLiveStation(reference, keyword, cell.get())) {
            stations.push_back(reference);
        }
        if (IsLiveMapMarker(reference, cell.get())) {
            mapMarkers.push_back(reference);
        }
    }
    if (directStation) {
        stations.push_back(directStation);
    }

    SortUnique(stations);
    SortUnique(mapMarkers);
    if (stations.size() != 1 || mapMarkers.size() != 1 || stations.front()->GetFormID() == mapMarkers.front()->GetFormID()) {
        return;
    }

    const auto cellId = cell->GetFormID();
    const auto displayedSystem = ReadDisplayedSystemForm(target.id, cellId);
    if (!displayedSystem) {
        return;
    }

    m_resolvedStation = stations.front();
    m_resolvedCourse = mapMarkers.front();
    m_resolvedMapId = target.id;
    m_displayedSystemFormId = *displayedSystem;
    target.resolvedTargetId = m_resolvedStation->GetFormID();
    target.resolvedCourseId = m_resolvedCourse->GetFormID();
    target.displayedSystemFormId = m_displayedSystemFormId;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
    REX::INFO("StationTargetBridge: resolved map={:08X} cell={:08X} refs={} station={:08X} course={:08X} displayed-system-form={:08X} elapsed-us={}",
        target.id, cellId, references->size(), target.resolvedTargetId, target.resolvedCourseId, m_displayedSystemFormId, elapsed);
}

bool StationTargetBridge::PrepareAssignment(FormID targetId)
{
    const auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(IsStarstationKeywordId);
    const auto cell = m_resolvedStation ? m_resolvedStation->parentCell : nullptr;
    if (targetId == 0 || !m_resolvedStation || !m_resolvedCourse || m_resolvedStation->GetFormID() != targetId || m_resolvedCourse->GetFormID() == targetId ||
        !IsLiveStation(m_resolvedStation, keyword, cell) || !IsLiveMapMarker(m_resolvedCourse, cell)) {
        Invalidate();
        return false;
    }

    m_assignmentTargetId = targetId;
    return true;
}

void StationTargetBridge::CancelAssignment()
{
    m_assignmentTargetId = 0;
}

bool StationTargetBridge::Assign(FormID targetId)
{
    const Reference reference = m_assignmentTargetId == targetId && m_resolvedStation && m_resolvedStation->GetFormID() == targetId ? m_resolvedStation : Reference {};

    const auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(
        IsStarstationKeywordId);
    const auto base = reference ? reference->GetBaseObject() : nullptr;
    if (!reference || reference->IsDeleted() || !base || base->IsDeleted() || !keyword || !reference->HasKeyword(keyword)) {
        m_resolvedStation.reset();
        m_resolvedCourse.reset();
        m_resolvedMapId = 0;
        m_displayedSystemFormId = 0;
        m_assignmentTargetId = 0;
        REX::ERROR("StationTargetBridge: refusing target {:08X}; retained REFR/keyword validation failed", targetId);
        return false;
    }

    RE::ShipHudTarget::Set(targetId);

    const auto observed = RE::ShipHudTarget::GetCurrent();
    m_resolvedStation.reset();
    m_resolvedCourse.reset();
    m_resolvedMapId = 0;
    m_displayedSystemFormId = 0;
    m_assignmentTargetId = 0;
    if (observed != targetId) {
        REX::ERROR("StationTargetBridge: target {:08X} did not commit; observed {:08X}", targetId, observed);
        return false;
    }

    REX::INFO("StationTargetBridge: assigned target {:08X}", targetId);
    return true;
}

void StationTargetBridge::Invalidate()
{
    m_resolvedStation.reset();
    m_resolvedCourse.reset();
    m_resolvedMapId = 0;
    m_displayedSystemFormId = 0;
    m_assignmentTargetId = 0;
}
