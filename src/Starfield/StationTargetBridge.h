#pragma once

#include "Selection/SelectionPolicy.h"

#include "RE/Starfield.h"

class StationTargetBridge final
{
public:
    void Resolve(TargetObservation& target);
    bool PrepareAssignment(FormID targetId);
    void CancelAssignment();
    bool Assign(FormID targetId);
    void Invalidate();

private:
    // Temporary CELL children are not guaranteed to remain globally discoverable.
    // Keep the exact validated selection alive through the map-close handoff.
    RE::NiPointer<RE::TESObjectREFR> m_resolvedStation;
    RE::NiPointer<RE::TESObjectREFR> m_resolvedCourse;
    FormID m_resolvedMapId {0};
    FormID m_displayedSystemFormId {0};
    FormID m_assignmentTargetId {0};
};
