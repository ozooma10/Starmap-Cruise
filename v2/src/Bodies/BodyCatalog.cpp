#include "Bodies/BodyCatalog.h"

#include <utility>

namespace
{
    BodyCatalogGeneration NextGeneration(BodyCatalogGeneration current)
    {
        current++;

        // Zero remains the invalid/uninitialized generation.
        if (current == 0) {
            current++;
        }

        return current;
    }
}

BodyCatalogGeneration BodyCatalog::BeginLoad()
{
    std::lock_guard lock {mutex_};

    generation_ = NextGeneration(generation_);
    status_ = BodyCatalogStatus::Loading;
    bodies_.clear();

    return generation_;
}

bool BodyCatalog::Publish(BodyCatalogGeneration generation, std::vector<IndexedBodyObservation> bodies)
{
    std::unordered_map<FormID, IndexedBodyObservation> indexedBodies;

    bool valid = true;

    for (auto& body : bodies) {
        if (body.id == 0 || body.systemId == 0 || !indexedBodies.emplace(body.id, body).second) {
            valid = false;
            break;
        }
    }

    std::lock_guard lock {mutex_};

    if (generation != generation_ || status_ != BodyCatalogStatus::Loading) {
        return false;
    }

    if (!valid) {
        bodies_.clear();
        status_ = BodyCatalogStatus::Failed;
        return false;
    }

    bodies_ = std::move(indexedBodies);
    status_ = BodyCatalogStatus::Ready;

    return true;
}

bool BodyCatalog::Fail(BodyCatalogGeneration generation)
{
    std::lock_guard lock {mutex_};

    if (generation != generation_ || status_ != BodyCatalogStatus::Loading) {
        return false;
    }

    bodies_.clear();
    status_ = BodyCatalogStatus::Failed;

    return true;
}

void BodyCatalog::Clear()
{
    std::lock_guard lock {mutex_};

    generation_ = NextGeneration(generation_);
    status_ = BodyCatalogStatus::Empty;
    bodies_.clear();
}

bool BodyCatalog::IsReady() const
{
    std::lock_guard lock {mutex_};
    return status_ == BodyCatalogStatus::Ready;
}

BodyCatalogStatus BodyCatalog::Status() const
{
    std::lock_guard lock {mutex_};
    return status_;
}

BodyCatalogGeneration BodyCatalog::CurrentGeneration() const
{
    std::lock_guard lock {mutex_};
    return generation_;
}

std::optional<IndexedBodyObservation> BodyCatalog::Find(FormID bodyId) const
{
    std::lock_guard lock {mutex_};

    if (status_ != BodyCatalogStatus::Ready) {
        return std::nullopt;
    }

    const auto found = bodies_.find(bodyId);

    if (found == bodies_.end()) {
        return std::nullopt;
    }

    return found->second;
}