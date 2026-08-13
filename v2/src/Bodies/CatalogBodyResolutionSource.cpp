#include "Bodies/CatalogBodyResolutionSource.h"

CatalogBodyResolutionSource::CatalogBodyResolutionSource(const LiveBodyProbe& liveBodies, const BodyCatalog& catalog) : liveBodies_(liveBodies), catalog_(catalog) {}

bool CatalogBodyResolutionSource::IsLiveBody(FormID bodyId) const
{
    return liveBodies_.IsLiveBody(bodyId);
}

bool CatalogBodyResolutionSource::IsBodyIndexReady() const
{
    return catalog_.IsReady();
}

std::optional<IndexedBodyObservation> CatalogBodyResolutionSource::FindIndexedBody(FormID bodyId) const
{
    return catalog_.Find(bodyId);
}