#pragma once

#include "Application/BodyResolutionSource.h"

class StarfieldBodyResolutionSource final : public BodyResolutionSource
{
public:
    //Installs a callsite thunk which copies SatelliteCSVData while Starfield owns its ComponentDB guard.
    //Failure disabeled remote routing; numeric/STDT same-system lookup remains available through the public acquiring wrappers.
    bool InitializeRemotePlanning();
    bool RemotePlanningAvailable() const;

    std::optional<ResolvedBody> ResolveBody(FormID bodyId) const override;
    std::optional<SystemIdentity> ResolveSystemIdentity(FormID formId) const;
    std::optional<SystemIdentity> ResolveCurrentSystem() const;
};
