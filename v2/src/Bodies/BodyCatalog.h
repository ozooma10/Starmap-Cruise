#pragma once

#include "Selection/SelectionPolicy.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

using BodyCatalogGeneration = std::uint64_t;

enum class BodyCatalogStatus : std::uint8_t
{
    Empty,
    Loading,
    Ready,
    Failed,
};

class BodyCatalog
{
public:
    BodyCatalogGeneration BeginLoad();

    bool Publish(BodyCatalogGeneration generation, std::vector<IndexedBodyObservation> bodies);

    bool Fail(BodyCatalogGeneration generation);

    void Clear();

    bool IsReady() const;

    BodyCatalogStatus Status() const;

    BodyCatalogGeneration CurrentGeneration() const;

    std::optional<IndexedBodyObservation> Find(FormID bodyId) const;

private:
    mutable std::mutex mutex_;

    BodyCatalogGeneration generation_ {0};
    BodyCatalogStatus status_ {BodyCatalogStatus::Empty};

    std::unordered_map<FormID, IndexedBodyObservation> bodies_;
};