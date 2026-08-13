#pragma once

#include "Map/MapSessionState.h"

struct BodyLookupResult
{
    bool isLiveBody {false};
    std::optional<ResolvedBody> body;
};

class BodyResolutionSource
{
public:
    virtual ~BodyResolutionSource() = default;

    virtual BodyLookupResult ResolveBody(FormID bodyId) const = 0;
};

class BodyResolver
{
public:
    explicit BodyResolver(const BodyResolutionSource& source);

    BodyResolutionUpdate Resolve(const TargetObservation& dossier) const;

private:
    const BodyResolutionSource& source_;
};
