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
        std::string spaceCellEditorID;
    };

    struct StationTarget
    {
        std::uint32_t referenceFormID{ 0 };
        std::uint32_t baseFormID{ 0 };
        std::uint32_t courseFormID{ 0 };
        std::string editorID;
    };

    struct IndexedBody
    {
        std::uint32_t formID{ 0 };
        GalaxyIdentity galaxy;
        std::string editorID;
    };

    void StartLoad();
    [[nodiscard]] bool Ready();
    [[nodiscard]] std::size_t Size();
    [[nodiscard]] std::optional<Entry> Lookup(std::uint32_t a_formID);
    [[nodiscard]] std::vector<IndexedBody> ParentPlanets(std::uint32_t a_moonFormID);
    [[nodiscard]] std::vector<IndexedBody> ParentBodies(std::uint32_t a_childFormID);
    [[nodiscard]] std::vector<IndexedBody> StationOrbitals(std::uint32_t a_cellFormID);
    [[nodiscard]] std::optional<std::uint32_t> LookupSystemRoot(std::uint32_t a_formID);
    [[nodiscard]] bool IsStationBase(std::uint32_t a_formID);
    [[nodiscard]] std::vector<StationTarget> StationTargets(std::uint32_t a_cellFormID);
}

