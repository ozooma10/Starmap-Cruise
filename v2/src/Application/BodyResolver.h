#pragma once

#include "Map/MapSessionState.h"

class BodyResolutionSource
{
public:
    virtual ~BodyResolutionSource() = default;

    virtual bool IsLiveBody(FormID bodyId) const = 0;
    virtual bool IsBodyIndexReady() const = 0;

    virtual std::optional<IndexedBodyObservation> FindIndexedBody(FormID bodyId) const = 0;
};

class BodyResolver
{
public:
    explicit BodyResolver(const BodyResolutionSource& source);

    BodyResolutionUpdate Resolve(const TargetObservation& dossier) const;

private:
    const BodyResolutionSource& source_;
};