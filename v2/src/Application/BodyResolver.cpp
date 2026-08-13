#include "Application/BodyResolver.h"

namespace
{
    bool IsSupported(ObservedTargetKind kind)
    {
        return kind == ObservedTargetKind::Planet || kind == ObservedTargetKind::Moon;
    }
}

BodyResolver::BodyResolver(const BodyResolutionSource& source) : source_(source) {}

BodyResolutionUpdate BodyResolver::Resolve(const TargetObservation& dossier) const
{
    BodyResolutionUpdate result {
        .dossierId = dossier.id,
    };

    if (dossier.id == 0 || !IsSupported(dossier.kind)) {
        return result;
    }

    auto lookup = source_.ResolveBody(dossier.id);
    result.dossierIsLiveBody = lookup.isLiveBody;
    result.resolvedBody = std::move(lookup.body);

    return result;
}
