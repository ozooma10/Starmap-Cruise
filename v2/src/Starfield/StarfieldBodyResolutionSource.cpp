#include "Starfield/StarfieldBodyResolutionSource.h"

#include "RE/Starfield.h"

#include <limits>

namespace
{
    constexpr REL::ID ResolveBodySystemId {124767};
    constexpr FormID MissingSystemId = std::numeric_limits<FormID>::max();

    using ResolveBodySystemIdFunction = FormID* (*)(FormID* systemId, FormID bodyId);
}

std::optional<ResolvedBody> StarfieldBodyResolutionSource::ResolveBody(FormID bodyId) const
{
    if (bodyId == 0) {
        return std::nullopt;
    }

    const auto* form = RE::TESForm::LookupByID(bodyId);

    if (!form || form->GetFormType() != RE::FormType::kPNDT || form->IsDeleted()) {
        return std::nullopt;
    }

    FormID systemId = MissingSystemId;
    static REL::Relocation<ResolveBodySystemIdFunction> resolveBodySystemId {ResolveBodySystemId};
    resolveBodySystemId(&systemId, bodyId);

    if (systemId != MissingSystemId) {
        return ResolvedBody {
            .id = bodyId,
            .systemId = systemId,
        };
    }

    return std::nullopt;
}
