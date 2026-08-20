#pragma once

#include "Application/BodyResolutionSource.h"

struct CurrentBodyLocation
{
    FormID bodyId {0};
    SystemIdentity system;
};

class StarfieldBodyResolutionSource final : public BodyResolutionSource
{
public:
    // Installs a callsite thunk which copies SatelliteCSVData while Starfield owns its ComponentDB guard.
    // Failure disables remote routing; numeric/STDT same-system lookup remains available through the public acquiring wrappers.
    bool InitializeRemotePlanning();

    std::optional<ResolvedBody> ResolveBody(FormID bodyId) const override;
    std::optional<SystemIdentity> ResolveSystemIdentity(FormID formId) const;
    std::optional<CurrentBodyLocation> ResolveCurrentLocation() const;
    std::optional<SystemIdentity> ResolveCurrentSystem() const;
};
