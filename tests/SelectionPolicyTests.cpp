#include "Selection/SelectionPolicy.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
            .currentSystemFormId = 0x1000,
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
                .system = {.starFormId = 0x1000, .numericId = 0x100},
                .remotePlan = ::RemoteTargetPlan {},
            },
        };
    }

    ::SelectionSnapshot ValidStation()
    {
        return {
            .sessionValid = true,
            .flying = true,
            .systemView = true,
            .currentSystemId = 0x11720,
            .currentSystemFormId = 0x5E60A,
            .highlightedMarkerCount = 1,
            .marker =
                {
                    .id = 0x1285A,
                    .kind = ::ObservedTargetKind::Station,
                    .displayName = "The Eye",
                    .resolvedTargetId = 0x12894,
                    .resolvedCourseId = 0x12895,
                    .displayedSystemFormId = 0x5E60A,
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

        Require(decision.destination->system == ::SystemIdentity {.starFormId = 0x1000, .numericId = 0x100}, "destination retained the wrong system identity");
        Require(!decision.requiresTravel, "same-system planet unexpectedly required travel");

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

    void TestExactStationIsEligibleWithoutDossier()
    {
        const auto decision = ::EvaluateSelection(ValidStation());

        Require(decision.IsEligible(), "resolved station was not eligible");
        Require(decision.destination->kind == ::DestinationKind::Station, "station observation produced the wrong destination kind");
        Require(decision.destination->targetId == 0x12894, "station destination retained the map CELL instead of the target REFR");
        Require(decision.destination->courseId == 0x12895, "station destination did not retain its distinct course marker");
        Require(decision.destination->system == ::SystemIdentity {.starFormId = 0x5E60A, .numericId = 0x11720}, "station destination retained the wrong system");
        Require(decision.destination->displayName == "The Eye", "station destination lost the marker name");
    }

    void TestUnresolvedStationIsUnavailable()
    {
        auto snapshot = ValidStation();
        snapshot.marker.resolvedTargetId = 0;

        const auto missingTarget = ::EvaluateSelection(snapshot);

        RequireDecision(missingTarget, ::SelectionAvailability::Disabled, ::SelectionReason::TargetSystemUnavailable);
        Require(!missingTarget.destination, "station without a target REFR produced a destination");

        snapshot = ValidStation();
        snapshot.marker.displayedSystemFormId.reset();

        const auto missingSystem = ::EvaluateSelection(snapshot);

        RequireDecision(missingSystem, ::SelectionAvailability::Disabled, ::SelectionReason::TargetSystemUnavailable);
        Require(!missingSystem.destination, "station without a displayed-system form produced a destination");

        snapshot = ValidStation();
        snapshot.currentSystemFormId.reset();

        const auto missingCurrentSystemForm = ::EvaluateSelection(snapshot);

        RequireDecision(missingCurrentSystemForm, ::SelectionAvailability::Disabled, ::SelectionReason::TargetSystemUnavailable);
        Require(!missingCurrentSystemForm.destination, "station without a current-system form produced a destination");
    }

    void TestStationWithoutCourseMarkerIsUnavailable()
    {
        auto snapshot = ValidStation();
        snapshot.marker.resolvedCourseId = 0;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::TargetSystemUnavailable);
        Require(!decision.destination, "station without a distinct course marker produced a destination");
    }

    void TestRemoteStationIsRejected()
    {
        auto snapshot = ValidStation();
        snapshot.marker.displayedSystemFormId = 0x5E5CB;

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::RemoteSystem);
        Require(!decision.destination, "remote station produced a current-system destination");
    }

    void TestSolStationIsEligible()
    {
        auto snapshot = ValidStation();
        snapshot.currentSystemId = 0;
        snapshot.currentSystemFormId = 0x5E5CB;
        snapshot.marker.displayedSystemFormId = 0x5E5CB;

        const auto decision = ::EvaluateSelection(snapshot);

        Require(decision.IsEligible(), "valid Sol station was rejected");
        Require(decision.destination->system.numericId == 0, "Sol station changed numeric system identity");
        Require(decision.destination->IsValid(), "Sol station destination was treated as invalid");
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

    void TestRemotePlanetIsEligibleWithCopiedPlan()
    {
        auto snapshot = ValidPlanet();
        snapshot.resolvedBody->system = {.starFormId = 0x2000, .numericId = 0x200};
        snapshot.resolvedBody->remotePlan = ::RemoteTargetPlan {
            .allowedWaypointIds = {0x77},
        };

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Eligible, ::SelectionReason::Eligible);
        Require(decision.requiresTravel, "remote planet did not require travel");
        Require(decision.destination.has_value(), "remote planet lost its destination");
        Require(decision.destination->system.starFormId == 0x2000, "remote planet lost its STDT identity");
        Require(decision.destination->remotePlan.allowedWaypointIds == std::vector<::FormID> {0x77}, "remote planet lost its copied plan");
    }

    void TestRemotePlanetWithoutPlanIsUnavailable()
    {
        auto snapshot = ValidPlanet();
        snapshot.resolvedBody->system = {.starFormId = 0x2000, .numericId = 0x200};
        snapshot.resolvedBody->remotePlan.reset();

        const auto decision = ::EvaluateSelection(snapshot);

        RequireDecision(decision, ::SelectionAvailability::Disabled, ::SelectionReason::TargetSystemUnavailable);
        Require(!decision.destination, "remote target without a native plan produced a destination");
    }

    void TestSolSystemIsEligible()
    {
        auto snapshot = ValidPlanet();
        snapshot.currentSystemId = 0;
        snapshot.currentSystemFormId = 0x5E5CB;
        snapshot.resolvedBody->system = {.starFormId = 0x5E5CB, .numericId = 0};

        const auto decision = ::EvaluateSelection(snapshot);

        Require(decision.IsEligible(), "valid Sol system zero was rejected");
        Require(decision.destination->system.numericId == 0, "Sol destination changed the system identity");
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
        TestExactStationIsEligibleWithoutDossier();
        TestUnresolvedStationIsUnavailable();
        TestStationWithoutCourseMarkerIsUnavailable();
        TestRemoteStationIsRejected();
        TestSolStationIsEligible();
        TestMissingHighlightIsDisabled();
        TestAmbiguousHighlightIsDisabled();
        TestMarkerAndDossierMustAgree();
        TestUnsupportedMarkerIsHidden();
        TestResolutionMustConfirmExactBody();
        TestRemotePlanetIsEligibleWithCopiedPlan();
        TestRemotePlanetWithoutPlanIsUnavailable();
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
