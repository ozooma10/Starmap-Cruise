#include "Presentation/ActionPresenter.h"

#include <utility>

namespace
{
    MapActionPresentation BuildPresentation(const ActionDecision& decision)
    {
        return {
            .control = decision.control,
            .enabled = decision.enabled,
            .label = decision.label,
            .holdLabel = decision.holdLabel,
        };
    }
}

PresentationResult ActionPresenter::Present(const ActionDecision& decision, MapActionView& view)
{
    auto presentation = BuildPresentation(decision);

    if (presented_ && *presented_ == presentation) {
        return {
            .changed = false,
            .applied = true,
        };
    }

    if (!view.Apply(presentation)) {
        return {
            .changed = true,
            .applied = false,
        };
    }

    presented_ = std::move(presentation);

    return {
        .changed = true,
        .applied = true,
    };
}

void ActionPresenter::Invalidate()
{
    presented_.reset();
}