#include "Domain/Destination.h"
#include "Domain/NonzeroCounter.h"
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
}

void RunDomainTests()
{
    TestSystemIdentityRequiresOnlyAStarForm();
    TestRemotePlanRejectsZeroAndDuplicateWaypoints();
    TestDestinationValidatesEveryIdentityComponent();
    TestDestinationIdentityIgnoresOnlyPresentationName();
    TestNonzeroCounterCoversInitialAdvanceAndRollover();
}
