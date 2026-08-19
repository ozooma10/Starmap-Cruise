#include "Presentation/ActionPolicy.h"

namespace
{
    ActionDecision HiddenAction(const SelectionDecision& selection)
    {
        return {
            .selectionReason = selection.reason,
            .requiresTravel = selection.requiresTravel,
        };
    }

    ActionControl SelectControl(const SelectionDecision& selection, const ActionContext& context)
    {
        if (selection.requiresTravel || selection.reason == SelectionReason::RemoteSystem) {
            return ActionControl::TapOnly;
        }

        if (context.cruiseStateWhenMapOpened != ObservedCruiseState::Inactive) {
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
    if (!selection.IsEligible() || !context.cruiseControlBound)
        return HiddenAction(selection);

    if (selection.requiresTravel) {
        if (!context.remoteRoutingAvailable || context.cruiseStateWhenMapOpened != ObservedCruiseState::Inactive)
            return HiddenAction(selection);
    }

    if (!context.vanillaActionEnabled)
        return HiddenAction(selection);

    ActionDecision decision {
        .control = SelectControl(selection, context),
        .enabled = true,
        .selectionReason = selection.reason,
        .label = selection.requiresTravel ? "JUMP THEN CRUISE" : "SET CRUISE TARGET",
        .destination = selection.destination,
        .requiresTravel = selection.requiresTravel,
    };

    if (decision.control == ActionControl::TapAndHold)
        decision.holdLabel = "HOLD TO CRUISE";

    return decision;
}
