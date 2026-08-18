#include "Map/MapSessionState.h"
#include "Selection/SelectionPolicy.h"
#include "TestSuites.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    constexpr ::MapSessionIdentity CurrentIdentity {
        .session = 7,
        .generation = 3,
    };

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    void OpenSession(::MapSessionState& state, std::optional<::FormID> currentSystemId = 0x100)
    {
        state.BeginMovie(CurrentIdentity.generation);

        const bool opened = state.Open({
            .identity = CurrentIdentity,
            .flying = true,
            .cruiseState = ::ObservedCruiseState::Inactive,
            .currentSystemId = currentSystemId,
        });

        Require(opened, "valid map session was not opened");
    }

    void PopulateExactPlanet(::MapSessionState& state)
    {
        Require(state.SetView(CurrentIdentity, ::MapView::System), "system-view update was rejected");

        Require(
            state.SetMarkers(
                CurrentIdentity,
                {
                    .highlightedCount = 1,
                    .highlighted =
                        {
                            .id = 0x10,
                            .kind = ::ObservedTargetKind::Planet,
                            .displayName = "Jemison Marker",
                        },
                }
            ),
            "marker update was rejected"
        );

        Require(
            state.SetDossier(
                CurrentIdentity,
                {
                    .id = 0x10,
                    .kind = ::ObservedTargetKind::Planet,
                    .displayName = "Jemison",
                },
                ::ResolvedBody {
                    .id = 0x10,
                    .systemId = 0x100,
                }
            ),
            "dossier update was rejected"
        );
    }

    void TestExactFeedsProduceEligibleSelection()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        const auto snapshot = state.Snapshot();
        const auto selection = ::EvaluateSelection(snapshot);

        Require(snapshot.sessionValid, "open session did not produce a valid snapshot");

        Require(selection.IsEligible(), "coherent provider state was not eligible");

        Require(selection.destination->targetId == 0x10, "selection retained the wrong target");

        Require(selection.destination->systemId == ::FormID {0x100}, "selection retained the wrong system");
    }

    void TestStaleSessionUpdateIsIgnored()
    {
        ::MapSessionState state;
        OpenSession(state);

        const ::MapSessionIdentity stale {
            .session = 6,
            .generation = 3,
        };

        const bool accepted = state.SetMarkers(
            stale,
            {
                .highlightedCount = 1,
                .highlighted = {
                    .id = 0x20,
                    .kind = ::ObservedTargetKind::Planet,
                },
            }
        );

        Require(!accepted, "stale session update was accepted");

        Require(state.Snapshot().highlightedMarkerCount == 0, "stale session changed marker state");
    }

    void TestViewChangeClearsTargetEvidence()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        Require(state.SetView(CurrentIdentity, ::MapView::Galaxy), "galaxy-view update was rejected");

        const auto snapshot = state.Snapshot();

        Require(!snapshot.systemView, "galaxy view remained a system view");

        Require(snapshot.highlightedMarkerCount == 0, "view change retained marker evidence");

        Require(snapshot.dossier.id == 0, "view change retained dossier evidence");

        Require(!snapshot.resolvedBody, "view change retained body resolution");
    }

    void TestRepeatedViewDoesNotClearEvidence()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        Require(state.SetView(CurrentIdentity, ::MapView::System), "repeated system-view update was rejected");

        const auto selection = ::EvaluateSelection(state.Snapshot());

        Require(selection.IsEligible(), "repeated view update cleared valid evidence");
    }

    void TestDossierUpdateAtomicallyReplacesResolution()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        Require(
            state.SetDossier(
                CurrentIdentity,
                {
                    .id = 0x20,
                    .kind = ::ObservedTargetKind::Planet,
                    .displayName = "Mars",
                },
                ::ResolvedBody {
                    .id = 0x20,
                    .systemId = 0x100,
                }
            ),
            "replacement dossier update was rejected"
        );

        const auto snapshot = state.Snapshot();

        Require(snapshot.dossier.id == 0x20, "replacement dossier retained the old identity");
        Require(snapshot.resolvedBody.has_value(), "replacement dossier lost its resolution");
        Require(snapshot.resolvedBody->id == 0x20, "replacement dossier inherited the old resolution");
    }

    void TestAmbiguousMarkersClearStoredTarget()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        state.SetMarkers(
            CurrentIdentity,
            {
                .highlightedCount = 2,
                .highlighted = {
                    .id = 0x20,
                    .kind = ::ObservedTargetKind::Planet,
                },
            }
        );

        const auto snapshot = state.Snapshot();
        const auto selection = ::EvaluateSelection(snapshot);

        Require(snapshot.marker.id == 0, "ambiguous update retained one marker as authoritative");

        Require(selection.reason == ::SelectionReason::AmbiguousTarget, "ambiguous markers produced the wrong rejection");
    }

    void TestLateCurrentSystemIsCapturedOnce()
    {
        ::MapSessionState state;
        OpenSession(state, std::nullopt);

        Require(!state.Snapshot().currentSystemId, "unresolved session invented a current system");

        Require(state.CaptureCurrentSystem(CurrentIdentity, 0x100), "first current-system resolution was rejected");

        Require(state.CaptureCurrentSystem(CurrentIdentity, 0x100), "repeated current-system resolution was rejected");

        Require(!state.CaptureCurrentSystem(CurrentIdentity, 0x200), "captured current system was rewritten");

        Require(state.Snapshot().currentSystemId == ::FormID {0x100}, "captured current system changed unexpectedly");
    }

    void TestSolSystemZeroIsCapturedOnce()
    {
        ::MapSessionState state;
        OpenSession(state, std::nullopt);

        Require(state.CaptureCurrentSystem(CurrentIdentity, 0), "valid Sol system zero was rejected");
        Require(state.CaptureCurrentSystem(CurrentIdentity, 0), "repeated Sol system resolution was rejected");
        Require(!state.CaptureCurrentSystem(CurrentIdentity, 0x100), "captured Sol system was rewritten");

        const auto snapshot = state.Snapshot();
        Require(snapshot.currentSystemId.has_value(), "captured Sol system lost its presence");
        Require(*snapshot.currentSystemId == 0, "captured Sol system changed identity");
    }

    void TestCurrentSystemFormIsCapturedOnce()
    {
        ::MapSessionState state;
        OpenSession(state);

        Require(!state.Snapshot().currentSystemFormId, "session invented a current-system form");
        Require(!state.CaptureCurrentSystemForm(CurrentIdentity, 0), "zero STDT FormID was accepted");
        Require(state.CaptureCurrentSystemForm(CurrentIdentity, 0x5E60A), "first current-system form was rejected");
        Require(state.CaptureCurrentSystemForm(CurrentIdentity, 0x5E60A), "repeated current-system form was rejected");
        Require(!state.CaptureCurrentSystemForm(CurrentIdentity, 0x5E5CB), "captured current-system form was rewritten");

        Require(state.SetView(CurrentIdentity, ::MapView::Galaxy), "view change was rejected");
        Require(state.Snapshot().currentSystemFormId == ::FormID {0x5E60A}, "view change cleared the session system form");

        Require(state.Close(CurrentIdentity), "session close was rejected");
        Require(!state.Snapshot().currentSystemFormId, "closed session retained its current-system form");
    }

    void TestMovieReplacementInvalidatesSession()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        state.BeginMovie(4);

        Require(!state.Snapshot().sessionValid, "movie replacement retained the old session");

        Require(!state.SetView(CurrentIdentity, ::MapView::System), "old movie identity updated replacement state");

        Require(
            !state.Open({
                .identity = CurrentIdentity,
                .flying = true,
                .currentSystemId = 0x100,
            }),
            "old movie generation reopened a session"
        );
    }

    void TestCruiseStateIsCapturedAndReset()
    {
        ::MapSessionState state;
        state.BeginMovie(CurrentIdentity.generation);

        Require(
            state.Open({
                .identity = CurrentIdentity,
                .flying = true,
                .cruiseState = ::ObservedCruiseState::Active,
                .currentSystemId = 0x100,
            }),
            "active-Cruise session was not opened"
        );

        Require(state.CruiseStateWhenOpened() == ::ObservedCruiseState::Active, "map session lost the observed Cruise state");
        Require(state.Close(CurrentIdentity), "active-Cruise session was not closed");
        Require(state.CruiseStateWhenOpened() == ::ObservedCruiseState::Unknown, "closed session retained its Cruise state");
    }

    void RunTests()
    {
        TestExactFeedsProduceEligibleSelection();
        TestStaleSessionUpdateIsIgnored();
        TestViewChangeClearsTargetEvidence();
        TestRepeatedViewDoesNotClearEvidence();
        TestDossierUpdateAtomicallyReplacesResolution();
        TestAmbiguousMarkersClearStoredTarget();
        TestLateCurrentSystemIsCapturedOnce();
        TestSolSystemZeroIsCapturedOnce();
        TestCurrentSystemFormIsCapturedOnce();
        TestMovieReplacementInvalidatesSession();
        TestCruiseStateIsCapturedAndReset();
    }
}

void RunMapSessionStateTests()
{
    RunTests();
}
