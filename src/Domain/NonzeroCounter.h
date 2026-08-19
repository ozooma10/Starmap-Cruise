#pragma once

#include <concepts>

namespace CFS
{
    template <std::unsigned_integral T> constexpr T AdvanceNonzeroCounter(T current) noexcept
    {
        ++current;
        if (current == 0)
            ++current;
        return current;
    }
}
