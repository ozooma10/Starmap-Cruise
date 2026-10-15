#include "Presentation/ActionPresenter.h"

#include <utility>

namespace
{
    MapActionPresentation BuildPresentation(const ActionDecision& decision, CFS::Localization::Language language)
    {
        const bool visible = decision.IsVisible();
        const bool showHold = visible && decision.enabled && decision.control == ActionControl::TapAndHold;
        return {
            .control = decision.control,
            .enabled = decision.enabled,
            .label = std::string {visible ? CFS::Localization::Translate(decision.labelId, language) : std::string_view {}},
            .holdLabel = std::string {showHold ? CFS::Localization::Translate(decision.holdLabelId, language) : std::string_view {}},
        };
    }
}

PresentationResult ActionPresenter::Present(const ActionDecision& decision, CFS::Localization::Language language, MapActionView& view)
{
    auto presentation = BuildPresentation(decision, language);

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
