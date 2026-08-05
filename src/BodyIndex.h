#pragma once

#include "Types.h"

#include <optional>
#include <string>
#include <vector>

namespace CFS::BodyIndex
{
    struct Entry
    {
        GalaxyIdentity galaxy;
        std::string editorID;
    };

    struct StationTarget
    {
        std::uint32_t referenceFormID{ 0 };
        std::uint32_t baseFormID{ 0 };
        std::string editorID;
    };

    void StartLoad();
    [[nodiscard]] bool Ready();
    [[nodiscard]] std::size_t Size();
    [[nodiscard]] std::optional<Entry> Lookup(std::uint32_t a_formID);
    [[nodiscard]] bool IsStationBase(std::uint32_t a_formID);
    [[nodiscard]] std::vector<StationTarget> StationTargets(std::uint32_t a_cellFormID);
}

