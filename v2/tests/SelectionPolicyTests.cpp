#include "Selection/SelectionPolicy.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error {std::string {message}};
        }
    }

    ::SelectionSnapshot ValidPlanet()
    {
        return {
            .sessionValid = true,
            .flying = true,
            .systemView = true,
            .currentSystemId = 0x100,
            .highlightedMarkerCount = 1,
            .marker =
                {
                    .id = 0x10,
                    .kind = ::ObservedTargetKind::Planet,
                    .displayName = "Jemison Marker",
                },
            .dossier =
                {
                    .id = 0x10,
                    .kind = ::ObservedTargetKind::Planet,
                    .displayName = "Jemison",
                },
            .resolvedBody = ::ResolvedBody {
                .id = 0x10,
                .systemId = 0x100,
            },
        };
    }

    void RequireDecision(const ::SelectionDecision& decision, ::SelectionAvailability availability, ::SelectionReason reason)
    {
        Require(decision.availability == availability, "selection availability did not match");
        Require(decision.reason == reason, "selection reason did not match");
    }

    void TestExactPlanetIsEligible()
    {
        const auto decision = ::EvaluateSelection(ValidPlanet());

        RequireDecision(decision, ::SelectionAvailability::Eligible, ::SelectionReason::Eligible);

        Require(decision.IsEligible(), "eligible decision did not contain a destination");

        Require(decision.destination->kind == ::DestinationKind::Planet, "planet observation produced the wrong destination kind");

        Require(decision.destination->targetId == 0x10, "destination retained the wrong target ID");

        Require(decision.destination->courseId == 0x10, "planet course ID did not match its target ID");

        Require(decision.destination->systemId == ::FormID {0x100}, "destination retained the wrong system ID");

        Require(decision.destination->displayName == "Jemison", "dossier name was not preferred");
    }

    void TestExactMoonIsEligible()
    {
        auto snapshot = ValidPlanet();

        snapshot.marker.kind = ::ObservedTargetKind::Moon;
        snapshot.dossier.kind = ::ObservedTargetKind::Moon;
        snapshot.marker.displayName = "Luna";
        snapshot.dossier.displayName.clear();

        const auto decision = ::EvaluateSelection(snapshot);

        Require(decision.IsEligible(), "exact moon was not eligible");

        Require(decision.destination->kind == ::DestinationKind::Moon, "moon observation produced the wrong destination kind");

        Require(decision.destination->displayName == "Luna", "empty dossier name did not fall back to marker name");
    }

    void TestMissingHighlightIsDisabled()
    {
        auto snapshot = ValidPlanet();
        snapshot.highlightedMarkerCount = 0;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::SelectDestination);

        Require(!decision.destination, "missing highlight produced a destination");
    }

    void TestAmbiguousHighlightIsDisabled()
    {
        auto snapshot = ValidPlanet();
        snapshot.highlightedMarkerCount = 2;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::AmbiguousTarget);
    }

    void TestMarkerAndDossierMustAgree()
    {
        auto snapshot = ValidPlanet();
        snapshot.dossier.id = 0x20;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::TargetDataUpdating);
    }

    void TestUnsupportedMarkerIsHidden()
    {
        auto snapshot = ValidPlanet();
        snapshot.marker.kind = ::ObservedTargetKind::Unsupported;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Hidden, ::SelectionReason::UnsupportedTarget);
    }

    void TestResolutionMustConfirmExactBody()
    {
        auto snapshot = ValidPlanet();
        snapshot.resolvedBody->id = 0x20;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::TargetSystemUnavailable);
    }

    void TestRemoteSystemIsNotPartOfMvp()
    {
        auto snapshot = ValidPlanet();
        snapshot.resolvedBody->systemId = 0x200;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::RemoteSystem);

        Require(!decision.destination, "remote-system rejection produced a destination");
    }

    void TestSolSystemIsEligible()
    {
        auto snapshot = ValidPlanet();
        snapshot.currentSystemId = 0;
        snapshot.resolvedBody->systemId = 0;

        const auto decision = ::EvaluateSelection(snapshot);

        Require(decision.IsEligible(), "valid Sol system zero was rejected");
        Require(decision.destination->systemId.has_value(), "Sol destination lost system presence");
        Require(*decision.destination->systemId == 0, "Sol destination changed the system identity");
        Require(decision.destination->IsValid(), "Sol destination was treated as invalid");
    }

    void TestMissingCurrentSystemIsUnavailable()
    {
        auto snapshot = ValidPlanet();
        snapshot.currentSystemId.reset();

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::CurrentSystemUnavailable);
    }

    void TestMissingBodySystemIsUnavailable()
    {
        auto snapshot = ValidPlanet();
        snapshot.resolvedBody.reset();

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::TargetSystemUnavailable);
    }

    void TestInvalidContextIsHidden()
    {
        auto snapshot = ValidPlanet();
        snapshot.sessionValid = false;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Hidden, ::SelectionReason::InactiveContext);
    }

    void RunTests()
    {
        TestExactPlanetIsEligible();
        TestExactMoonIsEligible();
        TestMissingHighlightIsDisabled();
        TestAmbiguousHighlightIsDisabled();
        TestMarkerAndDossierMustAgree();
        TestUnsupportedMarkerIsHidden();
        TestResolutionMustConfirmExactBody();
        TestRemoteSystemIsNotPartOfMvp();
        TestSolSystemIsEligible();
        TestMissingCurrentSystemIsUnavailable();
        TestMissingBodySystemIsUnavailable();
        TestInvalidContextIsHidden();
    }
}

void RunSelectionPolicyTests()
{
    RunTests();
}
