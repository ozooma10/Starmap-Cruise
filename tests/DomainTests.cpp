#include "Domain/Destination.h"
#include "Domain/NonzeroCounter.h"
#include "Domain/PlayerJumpState.h"
#include "Domain/ShipContext.h"
#include "Domain/ShipboardCruisePolicy.h"
#include "TestSuites.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    static_assert(CFS::AdvanceNonzeroCounter<std::uint32_t>(0) == 1);
    static_assert(CFS::AdvanceNonzeroCounter<std::uint32_t>(41) == 42);
    static_assert(CFS::AdvanceNonzeroCounter(std::numeric_limits<std::uint64_t>::max()) == 1);

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error {std::string {message}};
        }
    }

    Destination ValidDestination()
    {
        return {
            .kind = DestinationKind::Moon,
            .targetId = 0x10,
            .courseId = 0x20,
            .system = {.starFormId = 0x1000, .numericId = 0},
            .remotePlan = {.allowedWaypointIds = {0x30, 0x40}},
            .displayName = "Luna",
        };
    }

    void TestSystemIdentityRequiresOnlyAStarForm()
    {
        Require(!SystemIdentity {}.IsValid(), "empty system identity was valid");
        Require(SystemIdentity {.starFormId = 0x1000, .numericId = 0}.IsValid(), "Sol numeric identity zero invalidated a real STDT");
    }

    void TestRemotePlanRejectsZeroAndDuplicateWaypoints()
    {
        Require(RemoteTargetPlan {}.IsValid(), "direct-body empty plan was invalid");
        Require(RemoteTargetPlan {.allowedWaypointIds = {0x10, 0x20}}.IsValid(), "unique nonzero waypoint plan was invalid");
        Require(!RemoteTargetPlan {.allowedWaypointIds = {0}}.IsValid(), "zero waypoint was accepted");
        Require(!RemoteTargetPlan {.allowedWaypointIds = {0x10, 0x20, 0x10}}.IsValid(), "duplicate waypoint was accepted");
    }

    void TestDestinationValidatesEveryIdentityComponent()
    {
        const auto valid = ValidDestination();
        Require(valid.IsValid(), "fully populated destination was invalid");

        auto changed = valid;
        changed.targetId = 0;
        Require(!changed.IsValid(), "zero target was accepted");

        changed = valid;
        changed.courseId = 0;
        Require(!changed.IsValid(), "zero course was accepted");

        changed = valid;
        changed.system.starFormId = 0;
        Require(!changed.IsValid(), "missing STDT was accepted");

        changed = valid;
        changed.remotePlan.allowedWaypointIds.push_back(0x30);
        Require(!changed.IsValid(), "invalid remote plan was accepted");
    }

    void TestDestinationIdentityIgnoresOnlyPresentationName()
    {
        const auto baseline = ValidDestination();
        auto changed = baseline;
        changed.displayName = "Renamed Luna";
        Require(baseline.SameIdentityAs(changed), "display-name change altered destination identity");

        changed = baseline;
        changed.kind = DestinationKind::Planet;
        Require(!baseline.SameIdentityAs(changed), "kind change retained destination identity");

        changed = baseline;
        changed.targetId++;
        Require(!baseline.SameIdentityAs(changed), "target change retained destination identity");

        changed = baseline;
        changed.courseId++;
        Require(!baseline.SameIdentityAs(changed), "course change retained destination identity");

        changed = baseline;
        changed.system.numericId++;
        Require(!baseline.SameIdentityAs(changed), "system change retained destination identity");

        changed = baseline;
        changed.remotePlan.allowedWaypointIds.pop_back();
        Require(!baseline.SameIdentityAs(changed), "route-plan change retained destination identity");
    }

    void TestNonzeroCounterCoversInitialAdvanceAndRollover()
    {
        Require(CFS::AdvanceNonzeroCounter<std::uint32_t>(0) == 1, "zero counter did not advance to one");
        Require(CFS::AdvanceNonzeroCounter<std::uint32_t>(41) == 42, "ordinary counter did not advance once");
        Require(CFS::AdvanceNonzeroCounter(std::numeric_limits<std::uint64_t>::max()) == 1, "maximum counter did not roll over to one");
    }

    void TestPlayerJumpStatePreservesReplacementJumpInitiation()
    {
        PlayerJumpState state;
        state.Observe(1);
        Require(!state.Started() && !state.Completed(), "orphaned calculation state started a jump");

        state.Observe(0);
        Require(state.Started() && !state.Completed(), "FTL-style initiation without vanilla completion was not retained");
        state.Observe(1);
        state.Observe(3);
        Require(state.Started(), "replacement cancellation discarded the proven jump initiation");
        Require(!state.Completed(), "replacement cancellation was mistaken for vanilla completion");

        state.Reset();
        Require(!state.Started() && !state.Completed(), "jump reset retained travel proof");

        state.Observe(0);
        state.Observe(1);
        state.Observe(2);
        Require(state.Started() && state.Completed(), "exact vanilla jump sequence did not complete");
        Require(!state.InProgress(), "completed jump remained in progress");
    }

    void TestShipContextRequiresEveryFreeRoamSafetyGuard()
    {
        const ShipContext ready {
            .shipId = 0x10,
            .aboardPlayerShip = true,
            .inSpace = true,
            .playerPiloting = false,
            .landed = false,
            .docked = false,
            .loading = false,
            .jumpInProgress = false,
            .inCombat = false,
            .flightSettled = true,
        };
        Require(ready.IsShipboard(), "ready free-roam context was not shipboard");
        Require(ready.CanStartCruise(), "ready free-roam context could not start Cruise");
        Require(ready.SameShipAs(ready), "same ship identity did not match");

        auto changed = ready;
        changed.inSpace = false;
        Require(!changed.CanStartCruise(), "non-space ship could start Cruise");
        changed = ready;
        changed.landed = true;
        Require(!changed.CanStartCruise(), "landed ship could start Cruise");
        changed = ready;
        changed.docked = true;
        Require(!changed.CanStartCruise(), "docked ship could start Cruise");
        changed = ready;
        changed.loading = true;
        Require(!changed.CanStartCruise(), "loading transition could start Cruise");
        changed = ready;
        changed.jumpInProgress = true;
        Require(!changed.CanStartCruise(), "grav jump could start Cruise");
        changed = ready;
        changed.inCombat = true;
        Require(!changed.CanStartCruise(), "combat context could start Cruise");
        changed = ready;
        changed.flightSettled = false;
        Require(!changed.CanStartCruise(), "unsettled flight could start Cruise");
        changed = ready;
        changed.shipId++;
        Require(!ready.SameShipAs(changed), "different ship identities matched");
    }

    void TestShipboardCruisePolicySelectsCommandAndActivationPaths()
    {
        const ShipContext freeRoam {
            .shipId = 0x10,
            .aboardPlayerShip = true,
            .inSpace = true,
            .playerPiloting = false,
            .flightSettled = true,
        };
        auto piloting = freeRoam;
        piloting.playerPiloting = true;

        Require(SelectCruiseCommandPath(piloting, true, true) == CruiseCommandPath::Hud, "pilot context did not prefer the HUD path");
        Require(SelectCruiseCommandPath(piloting, false, true) == CruiseCommandPath::Unavailable, "pilot context fell back to native commands without a HUD");
        Require(SelectCruiseCommandPath(freeRoam, true, true) == CruiseCommandPath::Native, "free-roam context did not select the native path");
        Require(SelectCruiseCommandPath(freeRoam, true, false) == CruiseCommandPath::Unavailable, "free-roam context selected an unavailable native path");

        Require(
            DecideShipboardActivation(freeRoam, freeRoam, false, true, true) == ShipboardActivationMode::VanillaEligible,
            "normal native eligibility did not select the vanilla start mode");
        Require(
            DecideShipboardActivation(freeRoam, freeRoam, false, false, true) == ShipboardActivationMode::GuardedFreeRoam,
            "free-roam-only rejection did not select the guarded fallback");
        Require(
            DecideShipboardActivation(piloting, piloting, false, false, true) == ShipboardActivationMode::Rejected,
            "pilot context bypassed a failed vanilla eligibility check");
        Require(
            DecideShipboardActivation(freeRoam, freeRoam, true, true, true) == ShipboardActivationMode::Rejected,
            "active Cruise accepted another activation");

        auto unsafe = freeRoam;
        unsafe.loading = true;
        Require(
            DecideShipboardActivation(freeRoam, unsafe, false, false, true) == ShipboardActivationMode::Rejected,
            "live safety failure accepted the guarded fallback");
        unsafe = freeRoam;
        unsafe.shipId++;
        Require(
            DecideShipboardActivation(freeRoam, unsafe, false, true, true) == ShipboardActivationMode::Rejected,
            "changed ship identity accepted activation");
    }
}

void RunDomainTests()
{
    TestSystemIdentityRequiresOnlyAStarForm();
    TestRemotePlanRejectsZeroAndDuplicateWaypoints();
    TestDestinationValidatesEveryIdentityComponent();
    TestDestinationIdentityIgnoresOnlyPresentationName();
    TestNonzeroCounterCoversInitialAdvanceAndRollover();
    TestPlayerJumpStatePreservesReplacementJumpInitiation();
    TestShipContextRequiresEveryFreeRoamSafetyGuard();
    TestShipboardCruisePolicySelectsCommandAndActivationPaths();
}
