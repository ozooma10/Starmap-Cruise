#include "Starfield/StarfieldLiveBodyProbe.h"

#include "RE/Starfield.h"

bool StarfieldLiveBodyProbe::IsLiveBody(FormID bodyId) const
{
    if (bodyId == 0) {
        return false;
    }

    const auto* form = RE::TESForm::LookupByID(bodyId);

    return form && form->GetFormType() == RE::FormType::kPNDT;
}
