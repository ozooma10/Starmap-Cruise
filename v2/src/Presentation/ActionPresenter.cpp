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

    if (m_presented && *m_presented == presentation) {
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

    m_presented = std::move(presentation);

    return {
        .changed = true,
        .applied = true,
    };
}

void ActionPresenter::Invalidate()
{
    m_presented.reset();
}
