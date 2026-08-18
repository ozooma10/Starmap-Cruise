#pragma once

#include "Selection/SelectionPolicy.h"

class StationTargetBridge final
{
public:
    bool Initialize();
    void Resolve(TargetObservation& target) const;
    bool Assign(FormID targetId) const;

private:
    using SetTargetFunction = void (*)(FormID);

    SetTargetFunction m_setTarget {nullptr};
    const FormID* m_currentTarget {nullptr};
};
