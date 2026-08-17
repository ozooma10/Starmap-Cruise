#pragma once

#include "Application/BodyResolutionSource.h"

#include <optional>
#include <span>

std::optional<FormID> ResolveCurrentSystem(
    std::span<const FormID> bodyIds,
    const BodyResolutionSource& bodySource);
