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
};

struct TargetObservation
{
    FormID id {0};
    ObservedTargetKind kind {ObservedTargetKind::Unsupported};
    std::string displayName;
};

struct IndexedBodyObservation
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

    FormID currentSystemId {0};

    std::size_t highlightedMarkerCount {0};
    TargetObservation marker;
    TargetObservation dossier;

    // These values are copied from engine/index checks by the adapter.
    bool dossierIsLiveBody {false};
    bool bodyIndexReady {false};
    std::optional<IndexedBodyObservation> indexedBody;
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
    TargetNotLive,
    TargetDataLoading,
    TargetNotIndexed,
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