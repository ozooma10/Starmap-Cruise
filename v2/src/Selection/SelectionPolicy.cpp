#include "Selection/SelectionPolicy.h"

#include <utility>

namespace
{
    SelectionDecision Hidden(SelectionReason reason)
    {
        return {
            .availability = SelectionAvailability::Hidden,
            .reason = reason,
        };
    }

    SelectionDecision Disabled(SelectionReason reason)
    {
        return {
            .availability = SelectionAvailability::Disabled,
            .reason = reason,
        };
    }

    bool IsPlanetary(ObservedTargetKind kind)
    {
        return kind == ObservedTargetKind::Planet || kind == ObservedTargetKind::Moon;
    }

    DestinationKind ToDestinationKind(ObservedTargetKind kind)
    {
        if (kind == ObservedTargetKind::Moon) {
            return DestinationKind::Moon;
        }

        return DestinationKind::Planet;
    }
}

SelectionDecision EvaluateSelection(const SelectionSnapshot& snapshot)
{
    if (!snapshot.sessionValid || !snapshot.flying || !snapshot.systemView) {
        return Hidden(SelectionReason::InactiveContext);
    }

    if (!snapshot.currentSystemId) {
        return Disabled(SelectionReason::CurrentSystemUnavailable);
    }

    if (snapshot.highlightedMarkerCount == 0) {
        return Disabled(SelectionReason::SelectDestination);
    }

    if (snapshot.highlightedMarkerCount != 1) {
        return Disabled(SelectionReason::AmbiguousTarget);
    }

    if (snapshot.marker.id == 0 || !IsPlanetary(snapshot.marker.kind)) {
        return Hidden(SelectionReason::UnsupportedTarget);
    }

    if (snapshot.dossier.id == 0 || snapshot.marker.id != snapshot.dossier.id || snapshot.marker.kind != snapshot.dossier.kind) {
        return Disabled(SelectionReason::TargetDataUpdating);
    }

    if (!snapshot.resolvedBody || snapshot.resolvedBody->id != snapshot.dossier.id) {
        return Disabled(SelectionReason::TargetSystemUnavailable);
    }

    if (snapshot.resolvedBody->systemId != *snapshot.currentSystemId) {
        return Disabled(SelectionReason::RemoteSystem);
    }

    Destination destination {
        .kind = ToDestinationKind(snapshot.dossier.kind),
        .targetId = snapshot.dossier.id,
        .courseId = snapshot.dossier.id,
        .systemId = snapshot.resolvedBody->systemId,
        .displayName = snapshot.dossier.displayName.empty() ? snapshot.marker.displayName : snapshot.dossier.displayName,
    };

    return {
        .availability = SelectionAvailability::Eligible,
        .reason = SelectionReason::Eligible,
        .destination = std::move(destination),
    };
}
