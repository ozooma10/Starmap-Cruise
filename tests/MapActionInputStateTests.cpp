#include "Starfield/MapActionInputState.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    constexpr std::uint32_t Keyboard = 0;
    constexpr std::uint32_t Gamepad = 2;
    constexpr std::int32_t CruiseKey = 0x43;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error {std::string {message}};
        }
    }

    void TestReleasedControlRemainsAcceptableThroughActionDrain()
    {
        MapActionInputState state;
        state.Begin(Keyboard, CruiseKey);

        Require(!state.Filter(Keyboard, CruiseKey, true), "tracked map hold was filtered before acceptance");
        Require(!state.Filter(Keyboard, CruiseKey, false), "unaccepted map release was filtered");
        Require(state.AcceptAction() == Keyboard, "released control was lost before the action drain");
        Require(!state.AcceptAction(), "released control was accepted twice");
    }

    void TestReleasedControlExpiresAfterActionDrain()
    {
        MapActionInputState state;
        state.Begin(Keyboard, CruiseKey);

        Require(!state.Filter(Keyboard, CruiseKey, false), "unaccepted map release was filtered");
        state.ExpireReleased();
        Require(!state.AcceptAction(), "released control survived beyond its action drain");
    }

    void TestFrameExpirationPreservesPressedControl()
    {
        MapActionInputState state;
        state.Begin(Gamepad, CruiseKey);

        state.ExpireReleased();
        Require(state.AcceptAction() == Gamepad, "frame expiration discarded a control that was still pressed");
        state.ExpireReleased();
        Require(state.Filter(Gamepad, CruiseKey, false), "frame expiration discarded a claimed control before release");
    }

    void TestAcceptedControlIsClaimedThroughRelease()
    {
        MapActionInputState state;
        state.Begin(Gamepad, CruiseKey);

        Require(state.AcceptAction() == Gamepad, "accepted action lost its input device");
        Require(state.Filter(Gamepad, CruiseKey, true), "accepted input reached the map again");
        Require(state.Filter(Gamepad, CruiseKey, true), "carried cockpit input was not suppressed");
        Require(state.Filter(Gamepad, CruiseKey, false), "carried cockpit release was not suppressed");
        Require(!state.Filter(Gamepad, CruiseKey, true), "released control remained claimed");
    }

    void TestReleaseBeforeCloseEndsTheClaim()
    {
        MapActionInputState state;
        state.Begin(Keyboard, CruiseKey);
        const auto device = state.AcceptAction();

        Require(device == Keyboard, "accepted action lost the device needed for HUD replay");
        Require(state.Filter(Keyboard, CruiseKey, false), "accepted map release was not filtered");
        Require(!state.Filter(Keyboard, CruiseKey, false), "released control was suppressed after map close");
    }

    void TestOnlyTheExactControlIsFiltered()
    {
        MapActionInputState state;
        state.Begin(Keyboard, CruiseKey);
        Require(state.AcceptAction() == Keyboard, "action setup failed");

        Require(!state.Filter(Gamepad, CruiseKey, true), "different device was filtered");
        Require(!state.Filter(Keyboard, CruiseKey + 1, true), "different key was filtered");
        Require(state.Filter(Keyboard, CruiseKey, false), "exact release was not filtered");
    }

    void TestResetReleasesTheClaim()
    {
        MapActionInputState state;
        state.Begin(Keyboard, CruiseKey);
        Require(state.AcceptAction() == Keyboard, "action setup failed");

        state.Reset();
        Require(!state.Filter(Keyboard, CruiseKey, true), "reset action kept claiming the control");
        Require(!state.AcceptAction(), "reset action retained its input device");
    }

    void TestActionCanOnlyBeAcceptedOnce()
    {
        MapActionInputState state;
        Require(!state.AcceptAction(), "empty input state was accepted");

        state.Begin(Gamepad, CruiseKey);
        Require(state.AcceptAction() == Gamepad, "first action acceptance failed");
        Require(!state.AcceptAction(), "claimed action was accepted twice");
        Require(state.Filter(Gamepad, CruiseKey, false), "claimed release was not filtered");
        Require(!state.AcceptAction(), "released action became acceptable again without Begin");
    }

    void TestBeginAtomicallyReplacesPreviousControl()
    {
        MapActionInputState state;
        state.Begin(Keyboard, CruiseKey);
        Require(state.AcceptAction() == Keyboard, "old control setup failed");

        state.Begin(Gamepad, CruiseKey + 1);
        Require(!state.Filter(Keyboard, CruiseKey, true), "replacement retained the old claimed control");
        Require(state.AcceptAction() == Gamepad, "replacement lost its input device");
        Require(state.Filter(Gamepad, CruiseKey + 1, false), "replacement exact release was not filtered");
    }
}

void RunMapActionInputStateTests()
{
    TestReleasedControlRemainsAcceptableThroughActionDrain();
    TestReleasedControlExpiresAfterActionDrain();
    TestFrameExpirationPreservesPressedControl();
    TestAcceptedControlIsClaimedThroughRelease();
    TestReleaseBeforeCloseEndsTheClaim();
    TestOnlyTheExactControlIsFiltered();
    TestResetReleasesTheClaim();
    TestActionCanOnlyBeAcceptedOnce();
    TestBeginAtomicallyReplacesPreviousControl();
}
