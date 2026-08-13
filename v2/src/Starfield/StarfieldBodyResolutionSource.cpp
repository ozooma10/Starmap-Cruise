#include "Starfield/StarfieldBodyResolutionSource.h"

#include "RE/Starfield.h"

#include <limits>

namespace
{
    constexpr REL::ID ResolveBodySystemId {124767};
    constexpr FormID MissingSystemId = std::numeric_limits<FormID>::max();

    using ResolveBodySystemIdFunction = FormID* (*)(FormID* systemId, FormID bodyId);
}

BodyLookupResult StarfieldBodyResolutionSource::ResolveBody(FormID bodyId) const
{
    BodyLookupResult result;

    if (bodyId == 0) {
        return result;
    }

    const auto* form = RE::TESForm::LookupByID(bodyId);

    if (!form || form->GetFormType() != RE::FormType::kPNDT || form->IsDeleted()) {
        return result;
    }

    result.isLiveBody = true;

    FormID systemId = MissingSystemId;
    static REL::Relocation<ResolveBodySystemIdFunction> resolveBodySystemId {ResolveBodySystemId};
    resolveBodySystemId(&systemId, bodyId);

    if (systemId != MissingSystemId) {
        result.body = ResolvedBody {
            .id = bodyId,
            .systemId = systemId,
        };
    }

    return result;
}
