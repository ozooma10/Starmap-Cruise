#pragma once

#include <cstdint>

namespace CFS::Input
{
    struct Binding
    {
        std::int32_t code{ -1 };
        std::int32_t modifier{ -1 };
    };

    struct CruiseBindings
    {
        Binding keyboard;
        Binding mouse;
        Binding gamepad;
    };

    CruiseBindings ResolveCruiseBindings();
}
