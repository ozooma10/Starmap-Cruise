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

    void TestUnacceptedControlPassesThrough()
    {
        MapActionInputState state;
        state.Begin(Keyboard, CruiseKey);

        Require(!state.Filter(Keyboard, CruiseKey, true), "tracked map hold was filtered before acceptance");
        Require(!state.Filter(Keyboard, CruiseKey, false), "unaccepted map release was filtered");
        Require(!state.AcceptAction(), "released control remained available for acceptance");
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
}

void RunMapActionInputStateTests()
{
    TestUnacceptedControlPassesThrough();
    TestAcceptedControlIsClaimedThroughRelease();
    TestReleaseBeforeCloseEndsTheClaim();
    TestOnlyTheExactControlIsFiltered();
    TestResetReleasesTheClaim();
}
