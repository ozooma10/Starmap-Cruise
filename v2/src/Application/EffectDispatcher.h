#pragma once

#include "Navigation/NavigationRuntime.h"

#include <cstddef>
#include <optional>

class CruiseCommands
{
public:
    virtual ~CruiseCommands() = default;

    virtual bool CloseMap() = 0;
    virtual bool PressCruise() = 0;
    virtual bool RequestCourse(FormID courseId) = 0;
};

struct EffectDispatchResult
{
    bool handled{ false };
    std::size_t completedCount{ 0 };
    std::optional<Effect> failedEffect;

    bool Succeeded() const
    {
        return handled && !failedEffect.has_value();
    }
};

EffectDispatchResult DispatchEffects(const TransitionResult& transition, CruiseCommands& commands);