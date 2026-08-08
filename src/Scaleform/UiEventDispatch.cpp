#include "Scaleform/UiEventDispatch.h"

#include "REX/REX.h"

namespace CFS::ScaleformEvents
{
    bool DispatchUiEvent(RE::Scaleform::GFx::ASMovieRootBase* a_root,
        const char* a_type, const RE::Scaleform::GFx::Value* a_params)
    {
        using Value = RE::Scaleform::GFx::Value;

        Value manager;
        if (!a_root->GetVariable(&manager, "Shared.AS3.Data.BSUIDataManager") ||
            !(manager.IsObject() || manager.IsDisplayObject())) {
            REX::WARN("[ui] BSUIDataManager unavailable; '{}' not dispatched", a_type);
            return false;
        }

        Value type;
        a_root->CreateString(&type, a_type);
        Value args[2]{ type, a_params ? *a_params : Value{} };
        Value event;
        if (a_params)
            a_root->CreateObject(&event, "Shared.AS3.Events.CustomEvent", args, 2);
        else
            a_root->CreateObject(&event, "flash.events.Event", args, 1);
        if (event.IsObject() && manager.Invoke("dispatchEvent", nullptr, &event, 1))
            return true;
        return manager.Invoke("dispatchCustomEvent", nullptr, args, a_params ? 2 : 1);
    }
}
