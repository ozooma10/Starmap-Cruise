#include "TestSuites.h"

#include <cstddef>
#include <exception>
#include <iostream>
#include <string_view>

namespace
{
    struct TestSuite
    {
        std::string_view name;
        void (*run)();
    };

    constexpr TestSuite Suites[] {
        {"NavigationRuntime", &RunNavigationRuntimeTests},
        {"SelectionPolicy", &RunSelectionPolicyTests},
        {"ActionPolicy", &RunActionPolicyTests},
        {"MapSessionState", &RunMapSessionStateTests},
        {"CruiseRuntime", &RunCruiseRuntimeTests},
        {"ActionPresenter", &RunActionPresenterTests},
        {"MapObservationInbox", &RunMapObservationInboxTests},
    };

    bool RunSuite(const TestSuite& suite)
    {
        try {
            suite.run();
            std::cout << "[pass] " << suite.name << '\n';
            return true;
        } catch (const std::exception& error) {
            std::cerr << "[fail] " << suite.name << ": " << error.what() << '\n';
            return false;
        } catch (...) {
            std::cerr << "[fail] " << suite.name << ": unknown exception\n";
            return false;
        }
    }
}

int main()
{
    std::size_t failureCount = 0;

    for (const auto& suite : Suites) {
        if (!RunSuite(suite))
            ++failureCount;
    }

    if (failureCount != 0) {
        std::cerr << failureCount << " v2 test suite(s) failed\n";
        return 1;
    }

    std::cout << "All Cruise From Starmap v2 tests passed\n";
    return 0;
}
