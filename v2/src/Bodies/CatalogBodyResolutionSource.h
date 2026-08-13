#pragma once

#include "Application/BodyResolver.h"
#include "Bodies/BodyCatalog.h"

class LiveBodyProbe
{
public:
    virtual ~LiveBodyProbe() = default;

    virtual bool IsLiveBody(FormID bodyId) const = 0;
};

class CatalogBodyResolutionSource final : public BodyResolutionSource
{
public:
    CatalogBodyResolutionSource(const LiveBodyProbe& liveBodies, const BodyCatalog& catalog);

    bool IsLiveBody(FormID bodyId) const override;

    bool IsBodyIndexReady() const override;

    std::optional<IndexedBodyObservation> FindIndexedBody(FormID bodyId) const override;

private:
    const LiveBodyProbe& liveBodies_;
    const BodyCatalog& catalog_;
};