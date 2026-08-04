#pragma once

#include "Types.h"

#include <optional>
#include <string>

namespace CFS::BodyIndex
{
    struct Entry
    {
        GalaxyIdentity galaxy;
        std::string editorID;
    };

    void StartLoad();
    [[nodiscard]] bool Ready();
    [[nodiscard]] std::size_t Size();
    [[nodiscard]] std::optional<Entry> Lookup(std::uint32_t a_formID);
}

