#pragma once

#include <cstdint>

namespace CFS::Settings
{
    void Load();
    [[nodiscard]] bool Verbose();
    [[nodiscard]] std::int32_t GalaxyDiagnosticsMode();
}
