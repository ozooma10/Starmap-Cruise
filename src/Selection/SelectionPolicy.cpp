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

    if (snapshot.marker.id == 0) {
        return Hidden(SelectionReason::UnsupportedTarget);
    }

    if (snapshot.marker.kind == ObservedTargetKind::Station) {
        if (snapshot.marker.resolvedTargetId == 0 || snapshot.marker.resolvedCourseId == 0 || !snapshot.marker.displayedSystemFormId || *snapshot.marker.displayedSystemFormId == 0 ||
            !snapshot.currentSystemFormId || *snapshot.currentSystemFormId == 0) {
            return Disabled(SelectionReason::TargetSystemUnavailable);
        }

        if (snapshot.marker.displayedSystemFormId != snapshot.currentSystemFormId) {
            return Disabled(SelectionReason::RemoteSystem);
        }

        const SystemIdentity currentSystem {
            .starFormId = *snapshot.currentSystemFormId,
            .numericId = *snapshot.currentSystemId,
        };

        return {
            .availability = SelectionAvailability::Eligible,
            .reason = SelectionReason::Eligible,
            .destination = Destination {
                .kind = DestinationKind::Station,
                .targetId = snapshot.marker.resolvedTargetId,
                .courseId = snapshot.marker.resolvedCourseId,
                .system = currentSystem,
                .displayName = snapshot.marker.displayName,
            },
        };
    }

    if (!IsPlanetary(snapshot.marker.kind)) {
        return Hidden(SelectionReason::UnsupportedTarget);
    }

    if (snapshot.dossier.id == 0 || snapshot.marker.id != snapshot.dossier.id || snapshot.marker.kind != snapshot.dossier.kind) {
        return Disabled(SelectionReason::TargetDataUpdating);
    }

    if (!snapshot.resolvedBody || snapshot.resolvedBody->id != snapshot.dossier.id) {
        return Disabled(SelectionReason::TargetSystemUnavailable);
    }

    if (!snapshot.currentSystemFormId || *snapshot.currentSystemFormId == 0) {
        return Disabled(SelectionReason::CurrentSystemUnavailable);
    }

    const SystemIdentity currentSystem {
        .starFormId = *snapshot.currentSystemFormId,
        .numericId = *snapshot.currentSystemId,
    };

    if (!snapshot.resolvedBody->system.IsValid()) {
        return Disabled(SelectionReason::TargetSystemUnavailable);
    }

    const bool requiresTravel = snapshot.resolvedBody->system != currentSystem;
    if (requiresTravel && !snapshot.resolvedBody->remotePlan) {
        return Disabled(SelectionReason::TargetSystemUnavailable);
    }

    Destination destination {
        .kind = ToDestinationKind(snapshot.dossier.kind),
        .targetId = snapshot.dossier.id,
        .courseId = snapshot.dossier.id,
        .system = snapshot.resolvedBody->system,
        .remotePlan = snapshot.resolvedBody->remotePlan.value_or(RemoteTargetPlan {}),
        .displayName = snapshot.dossier.displayName.empty() ? snapshot.marker.displayName : snapshot.dossier.displayName,
    };

    return {
        .availability = SelectionAvailability::Eligible,
        .reason = SelectionReason::Eligible,
        .destination = std::move(destination),
        .requiresTravel = requiresTravel,
    };
}
