#pragma once

#include "Bodies/CatalogBodyResolutionSource.h"

class StarfieldLiveBodyProbe final : public LiveBodyProbe
{
public:
    bool IsLiveBody(FormID bodyId) const override;
};