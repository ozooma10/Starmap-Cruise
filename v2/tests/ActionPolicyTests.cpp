#include "Presentation/ActionPolicy.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error{ std::string{ message } };
    }

    ::Destination Jemison()
    {
        return {
            .kind = ::DestinationKind::Planet,
            .targetId = 0x10,
            .courseId = 0x10,
            .systemId = 0x100,
            .displayName = "Jemison",
        };
    }

    ::SelectionDecision EligibleSelection()
    {
        return {
            .availability = ::SelectionAvailability::Eligible,
            .reason = ::SelectionReason::Eligible,
            .destination = Jemison(),
        };
    }

    ::SelectionDecision DisabledSelection(
        ::SelectionReason reason)
    {
        return {
            .availability = ::SelectionAvailability::Disabled,
            .reason = reason,
        };
    }

    ::ActionContext ReadyContext()
    {
        return {
            .cruiseControlBound = true,
            .cruiseWasActiveWhenMapOpened = false,
            .cruiseEngageAvailable = true,
            .vanillaActionEnabled = true,
        };
    }

    void TestHiddenSelectionProducesNoControl()
    {
        const ::SelectionDecision selection;
        const auto action =
            ::EvaluateAction(selection, ReadyContext());

        Require(!action.IsVisible(),
            "hidden selection produced a visible control");

        Require(!action.CanHandleInput(),
            "hidden selection accepted input");

        Require(!action.destination,
            "hidden selection retained a destination");
    }

    void TestDisabledSelectionShowsReason()
    {
        const auto selection = DisabledSelection(
            ::SelectionReason::SelectDestination);

        const auto action =
            ::EvaluateAction(selection, ReadyContext());

        Require(action.IsVisible(),
            "disabled selection hid the action");

        Require(!action.enabled,
            "disabled selection enabled the action");

        Require(action.label == "HIGHLIGHT A DESTINATION",
            "disabled selection produced the wrong label");

        Require(!action.destination,
            "disabled selection retained a destination");
    }

    void TestUnboundControlFailsClosed()
    {
        auto context = ReadyContext();
        context.cruiseControlBound = false;

        const auto action =
            ::EvaluateAction(EligibleSelection(), context);

        Require(action.IsVisible(),
            "unbound Cruise control hid the action");

        Require(!action.CanHandleInput(),
            "unbound Cruise control accepted input");

        Require(
            action.label == "CRUISE CONTROL IS NOT BOUND",
            "unbound Cruise control produced the wrong label");

        Require(!action.destination,
            "unbound Cruise control retained an actionable target");
    }

    void TestReadyTargetUsesTapAndHold()
    {
        const auto action = ::EvaluateAction(
            EligibleSelection(),
            ReadyContext());

        Require(action.control ==
                ::ActionControl::TapAndHold,
            "ready target did not use tap-and-hold");

        Require(action.CanHandleInput(),
            "ready target did not accept input");

        Require(action.label == "SET CRUISE TARGET",
            "ready target produced the wrong tap label");

        Require(action.holdLabel == "HOLD TO CRUISE",
            "ready target produced the wrong hold label");

        Require(action.destination->targetId == 0x10,
            "ready action retained the wrong destination");
    }

    void TestAlreadyCruisingUsesTapOnly()
    {
        auto context = ReadyContext();
        context.cruiseWasActiveWhenMapOpened = true;

        const auto action =
            ::EvaluateAction(EligibleSelection(), context);

        Require(action.control == ::ActionControl::TapOnly,
            "active-Cruise map did not use tap-only");

        Require(action.CanHandleInput(),
            "active-Cruise tap was not actionable");

        Require(action.holdLabel.empty(),
            "active-Cruise tap exposed a hold label");
    }

    void TestCruiseCooldownUsesTapOnly()
    {
        auto context = ReadyContext();
        context.cruiseEngageAvailable = false;

        const auto action =
            ::EvaluateAction(EligibleSelection(), context);

        Require(action.control == ::ActionControl::TapOnly,
            "Cruise cooldown did not use tap-only");

        Require(action.CanHandleInput(),
            "Cruise cooldown prevented marking");

        Require(action.holdLabel.empty(),
            "Cruise cooldown exposed a hold label");
    }

    void TestVanillaActionStateCanDisableInput()
    {
        auto context = ReadyContext();
        context.vanillaActionEnabled = false;

        const auto action =
            ::EvaluateAction(EligibleSelection(), context);

        Require(action.IsVisible(),
            "vanilla-disabled action was hidden");

        Require(!action.CanHandleInput(),
            "vanilla-disabled action accepted input");

        Require(!action.destination,
            "vanilla-disabled action retained a destination");
    }

    void TestRemoteTargetIsDisabledForMvp()
    {
        const auto selection = DisabledSelection(
            ::SelectionReason::RemoteSystem);

        const auto action =
            ::EvaluateAction(selection, ReadyContext());

        Require(action.control == ::ActionControl::TapOnly,
            "remote MVP rejection exposed a hold control");

        Require(!action.CanHandleInput(),
            "remote MVP rejection accepted input");

        Require(
            action.label == "CURRENT-SYSTEM TARGETS ONLY",
            "remote MVP rejection produced the wrong label");
    }

    void RunTests()
    {
        TestHiddenSelectionProducesNoControl();
        TestDisabledSelectionShowsReason();
        TestUnboundControlFailsClosed();
        TestReadyTargetUsesTapAndHold();
        TestAlreadyCruisingUsesTapOnly();
        TestCruiseCooldownUsesTapOnly();
        TestVanillaActionStateCanDisableInput();
        TestRemoteTargetIsDisabledForMvp();
    }
}

void RunActionPolicyTests()
{
    RunTests();
}
