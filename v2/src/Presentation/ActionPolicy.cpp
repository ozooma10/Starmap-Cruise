#include "Presentation/ActionPolicy.h"

namespace
{
    const char* LabelFor(SelectionReason reason)
    {
        switch (reason) {
        case SelectionReason::InactiveContext:
            return "";

        case SelectionReason::CurrentSystemUnavailable:
            return "CURRENT SYSTEM UNAVAILABLE";

        case SelectionReason::SelectDestination:
            return "HIGHLIGHT A DESTINATION";

        case SelectionReason::AmbiguousTarget:
            return "TARGET IS AMBIGUOUS";

        case SelectionReason::UnsupportedTarget:
            return "";

        case SelectionReason::TargetDataUpdating:
            return "TARGET DATA IS UPDATING";

        case SelectionReason::TargetSystemUnavailable:
            return "TARGET DATA IS NOT AVAILABLE";

        case SelectionReason::RemoteSystem:
            return "CURRENT-SYSTEM TARGETS ONLY";

        case SelectionReason::Eligible:
            return "SET CRUISE TARGET";
        }

        return "";
    }

    ActionControl SelectControl(const SelectionDecision& selection, const ActionContext& context)
    {
        // Remote targets are shown without a hold action during the current-system-only MVP.
        if (selection.reason == SelectionReason::RemoteSystem) {
            return ActionControl::TapOnly;
        }

        // Cruise is already active, so another Cruise press is unnecessary.
        if (context.cruiseWasActiveWhenMapOpened) {
            return ActionControl::TapOnly;
        }

        // The target can still be marked while Cruise activation is temporarily unavailable.
        if (!context.cruiseEngageAvailable) {
            return ActionControl::TapOnly;
        }

        return ActionControl::TapAndHold;
    }
}

ActionDecision EvaluateAction(const SelectionDecision& selection, const ActionContext& context)
{
    if (selection.availability == SelectionAvailability::Hidden) {
        return {
            .selectionReason = selection.reason,
        };
    }

    ActionDecision decision {
        .control = SelectControl(selection, context),
        .selectionReason = selection.reason,
        .label = LabelFor(selection.reason),
    };

    if (!context.cruiseControlBound) {
        decision.label = "CRUISE CONTROL IS NOT BOUND";
        return decision;
    }

    if (!selection.IsEligible())
        return decision;

    if (!context.vanillaActionEnabled)
        return decision;

    decision.enabled = true;
    decision.destination = selection.destination;

    if (decision.control == ActionControl::TapAndHold)
        decision.holdLabel = "HOLD TO CRUISE";

    return decision;
}
