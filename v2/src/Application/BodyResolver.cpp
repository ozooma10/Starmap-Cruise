#include "Application/BodyResolver.h"

namespace
{
    bool IsSupported(ObservedTargetKind kind)
    {
        return kind == ObservedTargetKind::Planet || kind == ObservedTargetKind::Moon;
    }
}

BodyResolver::BodyResolver(const BodyResolutionSource& source) : source_(source)
{}

BodyResolutionUpdate BodyResolver::Resolve(const TargetObservation& dossier) const
{
    BodyResolutionUpdate result{
        .dossierId = dossier.id,
    };

    if (dossier.id == 0 || !IsSupported(dossier.kind)) {
        return result;
    }

    result.dossierIsLiveBody = source_.IsLiveBody(dossier.id);

    result.bodyIndexReady = source_.IsBodyIndexReady();

    if (!result.dossierIsLiveBody ||
        !result.bodyIndexReady) {
        return result;
    }

    result.indexedBody = source_.FindIndexedBody(dossier.id);

    return result;
}