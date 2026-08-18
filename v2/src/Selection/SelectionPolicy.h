#pragma once

#include "Domain/Destination.h"

#include <cstddef>
#include <optional>
#include <string>

enum class ObservedTargetKind : std::uint8_t
{
    Unsupported,
    Planet,
    Moon,
    Station,
};

struct TargetObservation
{
    FormID id {0};
    ObservedTargetKind kind {ObservedTargetKind::Unsupported};
    std::string displayName;

    FormID resolvedTargetId {0};
    FormID resolvedCourseId {0};
    // Native StarMap SystemState identity (STDT FormID), not a numeric galaxy-system ID.
    std::optional<FormID> displayedSystemFormId;
};

struct ResolvedBody
{
    FormID id {0};
    FormID systemId {0};
};

struct SelectionSnapshot
{
    // The adapter proves menu openness, session identity, and movie generation before setting this.
    bool sessionValid {false};

    bool flying {false};
    bool systemView {false};

    std::optional<FormID> currentSystemId;
    // Scaleform's current/player system identity (STDT FormID).
    std::optional<FormID> currentSystemFormId;

    std::size_t highlightedMarkerCount {0};
    TargetObservation marker;
    TargetObservation dossier;

    // This value is copied from the engine lookup by the adapter.
    std::optional<ResolvedBody> resolvedBody;
};

enum class SelectionAvailability : std::uint8_t
{
    Hidden,
    Disabled,
    Eligible,
};

enum class SelectionReason : std::uint8_t
{
    InactiveContext,
    CurrentSystemUnavailable,
    SelectDestination,
    AmbiguousTarget,
    UnsupportedTarget,
    TargetDataUpdating,
    TargetSystemUnavailable,
    RemoteSystem,
    Eligible,
};

struct SelectionDecision
{
    SelectionAvailability availability {SelectionAvailability::Hidden};

    SelectionReason reason {SelectionReason::InactiveContext};

    std::optional<Destination> destination;

    bool IsEligible() const
    {
        return availability == SelectionAvailability::Eligible && destination.has_value();
    }
};

SelectionDecision EvaluateSelection(const SelectionSnapshot& snapshot);
