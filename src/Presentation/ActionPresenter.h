#pragma once

#include "Presentation/ActionPolicy.h"

#include <optional>
#include <string>

struct MapActionPresentation
{
    ActionControl control {ActionControl::Hidden};
    bool enabled {false};

    std::string label;
    std::string holdLabel;

    friend bool operator==(const MapActionPresentation&, const MapActionPresentation&) = default;
};

class MapActionView
{
public:
    virtual ~MapActionView() = default;

    // True means the view successfully reflects the presentation.
    virtual bool Apply(const MapActionPresentation& presentation) = 0;
};

struct PresentationResult
{
    bool changed {false};
    bool applied {false};
};

class ActionPresenter
{
public:
    PresentationResult Present(const ActionDecision& decision, MapActionView& view);

    // Call when the Scaleform movie or input-device presentation changes.
    void Invalidate();

private:
    std::optional<MapActionPresentation> m_presented;
};
