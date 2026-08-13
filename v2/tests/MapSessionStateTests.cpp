#include "Map/MapSessionState.h"
#include "Selection/SelectionPolicy.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    constexpr ::MapSessionIdentity CurrentIdentity{
        .session = 7,
        .generation = 3,
    };

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error{ std::string{ message } };
    }

    void OpenSession(
        ::MapSessionState& state,
        ::FormID currentSystemId = 0x100)
    {
        state.BeginMovie(CurrentIdentity.generation);

        const bool opened = state.Open({
            .identity = CurrentIdentity,
            .flying = true,
            .cruiseWasActive = false,
            .currentSystemId = currentSystemId,
        });

        Require(opened, "valid map session was not opened");
    }

    void PopulateExactPlanet(::MapSessionState& state)
    {
        Require(
            state.SetView(
                CurrentIdentity,
                ::MapView::System),
            "system-view update was rejected");

        Require(
            state.SetMarkers(
                CurrentIdentity,
                {
                    .highlightedCount = 1,
                    .highlighted = {
                        .id = 0x10,
                        .kind = ::ObservedTargetKind::Planet,
                        .displayName = "Jemison Marker",
                    },
                }),
            "marker update was rejected");

        Require(
            state.SetDossier(
                CurrentIdentity,
                {
                    .id = 0x10,
                    .kind = ::ObservedTargetKind::Planet,
                    .displayName = "Jemison",
                }),
            "dossier update was rejected");

        Require(
            state.SetBodyResolution(
                CurrentIdentity,
                {
                    .dossierId = 0x10,
                    .dossierIsLiveBody = true,
                    .bodyIndexReady = true,
                    .indexedBody = ::IndexedBodyObservation{
                        .id = 0x10,
                        .systemId = 0x100,
                    },
                }),
            "body resolution was rejected");
    }

    void TestExactFeedsProduceEligibleSelection()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        const auto snapshot = state.Snapshot();
        const auto selection =
            ::EvaluateSelection(snapshot);

        Require(snapshot.sessionValid,
            "open session did not produce a valid snapshot");

        Require(selection.IsEligible(),
            "coherent provider state was not eligible");

        Require(selection.destination->targetId == 0x10,
            "selection retained the wrong target");

        Require(selection.destination->systemId == 0x100,
            "selection retained the wrong system");
    }

    void TestStaleSessionUpdateIsIgnored()
    {
        ::MapSessionState state;
        OpenSession(state);

        const ::MapSessionIdentity stale{
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
            });

        Require(!accepted,
            "stale session update was accepted");

        Require(
            state.Snapshot().highlightedMarkerCount == 0,
            "stale session changed marker state");
    }

    void TestViewChangeClearsTargetEvidence()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        Require(
            state.SetView(
                CurrentIdentity,
                ::MapView::Galaxy),
            "galaxy-view update was rejected");

        const auto snapshot = state.Snapshot();

        Require(!snapshot.systemView,
            "galaxy view remained a system view");

        Require(snapshot.highlightedMarkerCount == 0,
            "view change retained marker evidence");

        Require(snapshot.dossier.id == 0,
            "view change retained dossier evidence");

        Require(!snapshot.indexedBody,
            "view change retained body resolution");
    }

    void TestRepeatedViewDoesNotClearEvidence()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        Require(
            state.SetView(
                CurrentIdentity,
                ::MapView::System),
            "repeated system-view update was rejected");

        const auto selection =
            ::EvaluateSelection(state.Snapshot());

        Require(selection.IsEligible(),
            "repeated view update cleared valid evidence");
    }

    void TestDossierChangeClearsOldResolution()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        state.SetDossier(
            CurrentIdentity,
            {
                .id = 0x20,
                .kind = ::ObservedTargetKind::Planet,
                .displayName = "Mars",
            });

        const auto snapshot = state.Snapshot();

        Require(!snapshot.dossierIsLiveBody,
            "new dossier inherited old live-form proof");

        Require(!snapshot.bodyIndexReady,
            "new dossier inherited old index readiness");

        Require(!snapshot.indexedBody,
            "new dossier inherited old indexed identity");

        const bool acceptedOldResolution =
            state.SetBodyResolution(
                CurrentIdentity,
                {
                    .dossierId = 0x10,
                    .dossierIsLiveBody = true,
                    .bodyIndexReady = true,
                    .indexedBody =
                        ::IndexedBodyObservation{
                            .id = 0x10,
                            .systemId = 0x100,
                        },
                });

        Require(!acceptedOldResolution,
            "delayed old-dossier resolution was accepted");
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
            });

        const auto snapshot = state.Snapshot();
        const auto selection =
            ::EvaluateSelection(snapshot);

        Require(snapshot.marker.id == 0,
            "ambiguous update retained one marker as authoritative");

        Require(
            selection.reason ==
                ::SelectionReason::AmbiguousTarget,
            "ambiguous markers produced the wrong rejection");
    }

    void TestLateCurrentSystemIsCapturedOnce()
    {
        ::MapSessionState state;
        OpenSession(state, 0);

        Require(
            state.Snapshot().currentSystemId == 0,
            "unresolved session invented a current system");

        Require(
            state.CaptureCurrentSystem(
                CurrentIdentity,
                0x100),
            "first current-system resolution was rejected");

        Require(
            state.CaptureCurrentSystem(
                CurrentIdentity,
                0x100),
            "repeated current-system resolution was rejected");

        Require(
            !state.CaptureCurrentSystem(
                CurrentIdentity,
                0x200),
            "captured current system was rewritten");

        Require(
            state.Snapshot().currentSystemId == 0x100,
            "captured current system changed unexpectedly");
    }

    void TestMovieReplacementInvalidatesSession()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        state.BeginMovie(4);

        Require(!state.Snapshot().sessionValid,
            "movie replacement retained the old session");

        Require(
            !state.SetView(
                CurrentIdentity,
                ::MapView::System),
            "old movie identity updated replacement state");

        Require(
            !state.Open({
                .identity = CurrentIdentity,
                .flying = true,
                .currentSystemId = 0x100,
            }),
            "old movie generation reopened a session");
    }

    void RunTests()
    {
        TestExactFeedsProduceEligibleSelection();
        TestStaleSessionUpdateIsIgnored();
        TestViewChangeClearsTargetEvidence();
        TestRepeatedViewDoesNotClearEvidence();
        TestDossierChangeClearsOldResolution();
        TestAmbiguousMarkersClearStoredTarget();
        TestLateCurrentSystemIsCapturedOnce();
        TestMovieReplacementInvalidatesSession();
    }
}

void RunMapSessionStateTests()
{
    RunTests();
}
