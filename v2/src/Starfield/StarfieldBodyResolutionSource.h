#pragma once

#include "Application/BodyResolver.h"

class StarfieldBodyResolutionSource final : public BodyResolutionSource
{
public:
    // Call from the game-thread adapter after Starfield has finished loading data.
    // The result owns only copied IDs; no engine component pointer escapes.
    BodyLookupResult ResolveBody(FormID bodyId) const override;
};
