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
        Require(state.CaptureCurrentSystemForm(CurrentIdentity, 0x1000), "current STDT was not captured");
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
                    .system = {.starFormId = 0x1000, .numericId = 0x100},
                    .remotePlan = ::RemoteTargetPlan {},
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

        Require(selection.destination->system.numericId == ::FormID {0x100}, "selection retained the wrong system");
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
                    .system = {.starFormId = 0x1000, .numericId = 0x100},
                    .remotePlan = ::RemoteTargetPlan {},
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

    void TestShipContextIsCapturedAndReset()
    {
        ::MapSessionState state;
        state.BeginMovie(CurrentIdentity.generation);
        const ::ShipContext ship {
            .shipId = 0x1234,
            .aboardPlayerShip = true,
            .inSpace = true,
            .playerPiloting = false,
            .flightSettled = true,
        };

        Require(state.Open({
            .identity = CurrentIdentity,
            .flying = true,
            .shipContext = ship,
            .cruiseState = ::ObservedCruiseState::Inactive,
            .currentSystemId = 0x100,
        }), "shipboard session was not opened");
        Require(state.ShipContextWhenOpened() == ship, "map session lost its ship context");
        Require(state.Close(CurrentIdentity), "shipboard session was not closed");
        Require(state.ShipContextWhenOpened() == ::ShipContext {}, "closed session retained its ship context");
    }

    void TestInvalidOpenContextsAreRejected()
    {
        ::MapSessionState state;
        state.BeginMovie(CurrentIdentity.generation);

        auto context = ::MapOpenContext {
            .identity = CurrentIdentity,
            .flying = true,
            .currentSystemId = 0x100,
        };

        context.identity.session = 0;
        Require(!state.Open(context), "zero-session identity opened a map");

        context.identity = CurrentIdentity;
        context.identity.generation = 0;
        Require(!state.Open(context), "zero-generation identity opened a map");

        context.identity = CurrentIdentity;
        context.identity.generation--;
        Require(!state.Open(context), "old movie generation opened a map");

        Require(!state.Snapshot().sessionValid, "rejected open context changed session state");
        Require(!state.IsActive(CurrentIdentity), "rejected open context became active");
    }

    void TestInactiveAndStaleOperationsAreRejected()
    {
        ::MapSessionState state;
        state.BeginMovie(CurrentIdentity.generation);

        Require(!state.Close(CurrentIdentity), "inactive map accepted close");
        Require(!state.SetView(CurrentIdentity, ::MapView::System), "inactive map accepted view");
        Require(!state.SetMarkers(CurrentIdentity, {}), "inactive map accepted markers");
        Require(!state.SetDossier(CurrentIdentity, {}, std::nullopt), "inactive map accepted dossier");
        Require(!state.CaptureCurrentSystem(CurrentIdentity, 0x100), "inactive map accepted numeric system");
        Require(!state.CaptureCurrentSystemForm(CurrentIdentity, 0x1000), "inactive map accepted system form");

        OpenSession(state);
        const ::MapSessionIdentity stale {
            .session = CurrentIdentity.session + 1,
            .generation = CurrentIdentity.generation,
        };

        Require(!state.Close(stale), "stale identity closed current map");
        Require(!state.SetView(stale, ::MapView::Galaxy), "stale identity changed view");
        Require(!state.SetMarkers(stale, {.highlightedCount = 1}), "stale identity changed markers");
        Require(!state.SetDossier(stale, {.id = 0x20}, std::nullopt), "stale identity changed dossier");
        Require(!state.CaptureCurrentSystem(stale, 0x200), "stale identity changed numeric system");
        Require(!state.CaptureCurrentSystemForm(stale, 0x2000), "stale identity changed system form");
        Require(!state.IsActive(stale), "stale identity reported active");
        Require(state.IsActive(CurrentIdentity), "current identity stopped reporting active");
    }

    void TestCloseResetsCompleteSnapshot()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        Require(state.Close(CurrentIdentity), "current session close was rejected");
        const auto snapshot = state.Snapshot();
        Require(!snapshot.sessionValid, "closed session remained valid");
        Require(!snapshot.flying && !snapshot.systemView, "closed session retained map context");
        Require(!snapshot.currentSystemId && !snapshot.currentSystemFormId, "closed session retained system identity");
        Require(snapshot.highlightedMarkerCount == 0 && snapshot.marker.id == 0, "closed session retained marker state");
        Require(snapshot.dossier.id == 0 && !snapshot.resolvedBody, "closed session retained dossier state");
        Require(!state.Close(CurrentIdentity), "repeated close was accepted");
    }

    void TestOpeningNewSessionReplacesAllOldEvidence()
    {
        ::MapSessionState state;
        OpenSession(state);
        PopulateExactPlanet(state);

        const ::MapSessionIdentity replacement {
            .session = CurrentIdentity.session + 1,
            .generation = CurrentIdentity.generation,
        };
        Require(
            state.Open({
                .identity = replacement,
                .flying = false,
                .cruiseState = ::ObservedCruiseState::Unknown,
                .currentSystemId = std::nullopt,
            }),
            "replacement session was rejected"
        );

        const auto snapshot = state.Snapshot();
        Require(snapshot.sessionValid && !snapshot.flying, "replacement context was not captured");
        Require(!snapshot.currentSystemId && !snapshot.currentSystemFormId, "replacement inherited old system identity");
        Require(snapshot.highlightedMarkerCount == 0 && snapshot.dossier.id == 0 && !snapshot.resolvedBody, "replacement inherited old target evidence");
        Require(!state.IsActive(CurrentIdentity) && state.IsActive(replacement), "replacement retained the old active identity");
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
        TestShipContextIsCapturedAndReset();
        TestInvalidOpenContextsAreRejected();
        TestInactiveAndStaleOperationsAreRejected();
        TestCloseResetsCompleteSnapshot();
        TestOpeningNewSessionReplacesAllOldEvidence();
    }
}

void RunMapSessionStateTests()
{
    RunTests();
}
