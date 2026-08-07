#pragma once

#include "Types.h"

namespace CFS::Settings
{
    void Load();
    [[nodiscard]] bool ShowMarker();
    [[nodiscard]] bool ShowDestinationName();
    [[nodiscard]] bool Verbose();
}

