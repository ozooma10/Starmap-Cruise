#pragma once

#include "Selection/SelectionPolicy.h"

class BodyResolutionSource
{
public:
    virtual ~BodyResolutionSource() = default;

    virtual std::optional<ResolvedBody> ResolveBody(FormID bodyId) const = 0;
};
