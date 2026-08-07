#pragma once

#include <cstdint>
#include <optional>

namespace CFS::Input
{
    struct Binding
    {
        std::int32_t code{ -1 };
        std::int32_t modifier{ -1 };

        [[nodiscard]] explicit operator bool() const noexcept { return code >= 0; }
    };

    struct CruiseBindings
    {
        Binding keyboard;
        Binding mouse;
        Binding gamepad;
    };

    // Returns nullopt when the live ControlMap itself cannot be proven safe.
    [[nodiscard]] std::optional<CruiseBindings> ResolveCruiseBindings();
}
