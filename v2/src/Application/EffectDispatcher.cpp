#include "Application/EffectDispatcher.h"

#include <variant>

namespace
{
    struct DispatchVisitor
    {
        CruiseCommands& commands;

        bool operator()(const ::CloseMap&) const
        {
            return commands.CloseMap();
        }

        bool operator()(const ::PressCruise&) const
        {
            return commands.PressCruise();
        }

        bool operator()(const ::RequestCourse& effect) const
        {
            return commands.RequestCourse(effect.courseId);
        }
    };
}

EffectDispatchResult DispatchEffects(const TransitionResult& transition, CruiseCommands& commands)
{
    EffectDispatchResult result {
        .handled = transition.handled,
    };

    if (!transition.handled) {
        return result;
    }

    for (const auto& effect : transition.effects) {
        if (!std::visit(DispatchVisitor {commands}, effect)) {
            result.failedEffect = effect;
            return result;
        }

        ++result.completedCount;
    }

    return result;
}