#pragma once

#include "Selection/SelectionPolicy.h"

#include <optional>
#include <string>

enum class ActionControl : std::uint8_t
{
    Hidden,
    TapOnly,
    TapAndHold,
};

struct ActionContext
{
    // The Cruise action has a binding for the active input device.
    bool cruiseControlBound{ false };

    // Captured when this Starmap session opened.
    bool cruiseWasActiveWhenMapOpened{ false };

    // The stock cockpit Cruise control can currently activate Cruise.
    // False includes cooldown and other temporarily unavailable states.
    bool cruiseEngageAvailable{ false };

    // Copied from the vanilla Starmap button state.
    bool vanillaActionEnabled{ false };
};

struct ActionDecision
{
    ActionControl control{ ActionControl::Hidden };
    bool enabled{ false };

    SelectionReason selectionReason{ SelectionReason::InactiveContext };

    std::string label;
    std::string holdLabel;

    std::optional<Destination> destination;

    bool IsVisible() const
    {
        return control != ActionControl::Hidden;
    }

    bool CanHandleInput() const
    {
        return enabled && destination.has_value();
    }
};

ActionDecision EvaluateAction(const SelectionDecision& selection, const ActionContext& context);
