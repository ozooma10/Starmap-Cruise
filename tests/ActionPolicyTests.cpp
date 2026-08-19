#include "Presentation/ActionPolicy.h"
#include "TestSuites.h"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    ::Destination Jemison()
    {
        return {
            .kind = ::DestinationKind::Planet,
            .targetId = 0x10,
            .courseId = 0x10,
            .system = {.starFormId = 0x1000, .numericId = 0x100},
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

    ::SelectionDecision DisabledSelection(::SelectionReason reason)
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
            .cruiseStateWhenMapOpened = ::ObservedCruiseState::Inactive,
            .cruiseEngageAvailable = true,
            .vanillaActionEnabled = true,
            .remoteRoutingAvailable = true,
        };
    }

    void TestHiddenSelectionProducesNoControl()
    {
        const ::SelectionDecision selection;
        const auto action = ::EvaluateAction(selection, ReadyContext());

        Require(!action.IsVisible(), "hidden selection produced a visible control");

        Require(!action.CanHandleInput(), "hidden selection accepted input");

        Require(!action.destination, "hidden selection retained a destination");
    }

    void TestDisabledSelectionHidesControl()
    {
        const auto selection = DisabledSelection(::SelectionReason::SelectDestination);

        const auto action = ::EvaluateAction(selection, ReadyContext());

        Require(!action.IsVisible(), "disabled selection showed an action");

        Require(!action.enabled, "disabled selection enabled the action");

        Require(action.label.empty(), "disabled selection retained a label");

        Require(!action.destination, "disabled selection retained a destination");
    }

    void TestUnboundControlFailsClosed()
    {
        auto context = ReadyContext();
        context.cruiseControlBound = false;

        const auto action = ::EvaluateAction(EligibleSelection(), context);

        Require(!action.IsVisible(), "unbound Cruise control showed the action");

        Require(!action.CanHandleInput(), "unbound Cruise control accepted input");

        Require(action.label.empty(), "unbound Cruise control retained a label");

        Require(!action.destination, "unbound Cruise control retained an actionable target");
    }

    void TestReadyTargetUsesTapAndHold()
    {
        const auto action = ::EvaluateAction(EligibleSelection(), ReadyContext());

        Require(action.control == ::ActionControl::TapAndHold, "ready target did not use tap-and-hold");

        Require(action.CanHandleInput(), "ready target did not accept input");

        Require(action.label == "SET CRUISE TARGET", "ready target produced the wrong tap label");

        Require(action.holdLabel == "HOLD TO CRUISE", "ready target produced the wrong hold label");

        Require(action.destination->targetId == 0x10, "ready action retained the wrong destination");
    }

    void TestAlreadyCruisingUsesTapOnly()
    {
        auto context = ReadyContext();
        context.cruiseStateWhenMapOpened = ::ObservedCruiseState::Active;

        const auto action = ::EvaluateAction(EligibleSelection(), context);

        Require(action.control == ::ActionControl::TapOnly, "active-Cruise map did not use tap-only");

        Require(action.CanHandleInput(), "active-Cruise tap was not actionable");

        Require(action.holdLabel.empty(), "active-Cruise tap exposed a hold label");
    }

    void TestUnknownCruiseStateUsesTapOnly()
    {
        auto context = ReadyContext();
        context.cruiseStateWhenMapOpened = ::ObservedCruiseState::Unknown;

        const auto action = ::EvaluateAction(EligibleSelection(), context);

        Require(action.control == ::ActionControl::TapOnly, "unknown Cruise state exposed a hold action");
        Require(action.CanHandleInput(), "unknown Cruise state prevented safe marking");
        Require(action.holdLabel.empty(), "unknown Cruise state exposed a hold label");
    }

    void TestCruiseCooldownUsesTapOnly()
    {
        auto context = ReadyContext();
        context.cruiseEngageAvailable = false;

        const auto action = ::EvaluateAction(EligibleSelection(), context);

        Require(action.control == ::ActionControl::TapOnly, "Cruise cooldown did not use tap-only");

        Require(action.CanHandleInput(), "Cruise cooldown prevented marking");

        Require(action.holdLabel.empty(), "Cruise cooldown exposed a hold label");
    }

    void TestVanillaActionStateCanDisableInput()
    {
        auto context = ReadyContext();
        context.vanillaActionEnabled = false;

        const auto action = ::EvaluateAction(EligibleSelection(), context);

        Require(!action.IsVisible(), "vanilla-disabled action remained visible");

        Require(!action.CanHandleInput(), "vanilla-disabled action accepted input");

        Require(!action.destination, "vanilla-disabled action retained a destination");
    }

    void TestRemoteTargetUsesOneAction()
    {
        auto selection = EligibleSelection();
        selection.requiresTravel = true;
        selection.destination->system = {.starFormId = 0x2000, .numericId = 0x200};

        const auto action = ::EvaluateAction(selection, ReadyContext());

        Require(action.control == ::ActionControl::TapOnly, "remote action exposed a hold control");
        Require(action.CanHandleInput(), "ready remote action rejected input");
        Require(action.label == "JUMP THEN CRUISE", "remote action produced the wrong label");
        Require(action.requiresTravel, "remote action lost its travel flag");
    }

    void TestRemoteTargetRejectsActiveCruiseAndMissingBindings()
    {
        auto selection = EligibleSelection();
        selection.requiresTravel = true;

        auto context = ReadyContext();
        context.cruiseStateWhenMapOpened = ::ObservedCruiseState::Active;
        auto action = ::EvaluateAction(selection, context);
        Require(!action.IsVisible(), "active-Cruise remote rejection remained visible");
        Require(!action.CanHandleInput(), "remote action tried to exit active Cruise");

        context = ReadyContext();
        context.remoteRoutingAvailable = false;
        action = ::EvaluateAction(selection, context);
        Require(!action.IsVisible(), "unavailable native route bridge remained visible");
        Require(!action.CanHandleInput(), "unavailable native route bridge accepted input");
    }

    void TestRemoteStationReasonRemainsDisabled()
    {
        const auto action = ::EvaluateAction(DisabledSelection(::SelectionReason::RemoteSystem), ReadyContext());

        Require(!action.IsVisible(), "remote station rejection remained visible");
        Require(!action.CanHandleInput(), "remote station rejection accepted input");
        Require(action.label.empty(), "remote station rejection retained a label");
    }

    void TestEveryDisabledReasonHidesControl()
    {
        constexpr std::array reasons {
            ::SelectionReason::InactiveContext,
            ::SelectionReason::CurrentSystemUnavailable,
            ::SelectionReason::SelectDestination,
            ::SelectionReason::AmbiguousTarget,
            ::SelectionReason::UnsupportedTarget,
            ::SelectionReason::TargetDataUpdating,
            ::SelectionReason::TargetSystemUnavailable,
            ::SelectionReason::RemoteSystem,
            ::SelectionReason::Eligible,
            static_cast<::SelectionReason>(0xFF),
        };

        for (const auto reason : reasons) {
            const auto action = ::EvaluateAction(DisabledSelection(reason), ReadyContext());
            Require(!action.IsVisible(), "disabled reason showed a control");
            Require(!action.CanHandleInput(), "disabled reason accepted input");
            Require(action.label.empty(), "disabled reason retained a label");
        }
    }

    void TestEligibleSelectionRequiresAValidDestination()
    {
        auto selection = EligibleSelection();
        selection.destination.reset();

        auto action = ::EvaluateAction(selection, ReadyContext());
        Require(!selection.IsEligible(), "eligible flag without a destination passed validation");
        Require(!action.CanHandleInput(), "eligible flag without a destination accepted input");
        Require(!action.destination, "eligible flag without a destination invented one");

        selection = EligibleSelection();
        selection.destination->targetId = 0;
        action = ::EvaluateAction(selection, ReadyContext());
        Require(!selection.IsEligible(), "invalid destination passed selection validation");
        Require(!action.CanHandleInput(), "invalid destination accepted input");
        Require(!action.destination, "invalid destination escaped into the action");
    }

    void TestRemoteUnknownCruiseStateFailsClosed()
    {
        auto selection = EligibleSelection();
        selection.requiresTravel = true;

        auto context = ReadyContext();
        context.cruiseStateWhenMapOpened = ::ObservedCruiseState::Unknown;

        const auto action = ::EvaluateAction(selection, context);
        Require(!action.IsVisible(), "remote unknown-state action remained visible");
        Require(!action.CanHandleInput(), "remote unknown-state action accepted input");
        Require(!action.destination, "remote unknown-state action retained a destination");
    }

    void TestRemoteActionStillRequiresVanillaEnablement()
    {
        auto selection = EligibleSelection();
        selection.requiresTravel = true;
        auto context = ReadyContext();
        context.vanillaActionEnabled = false;

        const auto action = ::EvaluateAction(selection, context);
        Require(!action.IsVisible(), "vanilla-disabled remote action remained visible");
        Require(!action.CanHandleInput(), "vanilla-disabled remote action accepted input");
        Require(!action.destination, "vanilla-disabled remote action retained its destination");
    }

    void RunTests()
    {
        TestHiddenSelectionProducesNoControl();
        TestDisabledSelectionHidesControl();
        TestUnboundControlFailsClosed();
        TestReadyTargetUsesTapAndHold();
        TestAlreadyCruisingUsesTapOnly();
        TestUnknownCruiseStateUsesTapOnly();
        TestCruiseCooldownUsesTapOnly();
        TestVanillaActionStateCanDisableInput();
        TestRemoteTargetUsesOneAction();
        TestRemoteTargetRejectsActiveCruiseAndMissingBindings();
        TestRemoteStationReasonRemainsDisabled();
        TestEveryDisabledReasonHidesControl();
        TestEligibleSelectionRequiresAValidDestination();
        TestRemoteUnknownCruiseStateFailsClosed();
        TestRemoteActionStillRequiresVanillaEnablement();
    }
}

void RunActionPolicyTests()
{
    RunTests();
}
