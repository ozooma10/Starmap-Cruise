#include "Starfield/MapObservationInbox.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    MapSessionIdentity OpenMap(MapObservationInbox& inbox)
    {
        inbox.RecordMovieCreated(100);
        inbox.RecordLifecycle(true);

        const auto observations = inbox.Drain();
        Require(observations.movieCreated, "movie creation was not recorded");
        Require(observations.movieGeneration == 1, "movie retained the wrong generation");
        Require(observations.movieBornTicks == 100, "movie retained the wrong creation time");
        Require(observations.lifecycleCount == 1, "open was not recorded");
        Require(observations.lifecycle[0].opening, "open was recorded as a close");

        return observations.lifecycle[0].identity;
    }

    void TestCloseKeepsOpenIdentity()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordMapData(identity, MapView::System, 0x1234, 0xABCD);
        inbox.RecordMarkers(identity, {
            .highlightedCount = 1,
            .highlighted = {
                .id = 0x5678,
                .kind = ObservedTargetKind::Planet,
                .displayName = "Jemison",
            },
        });
        inbox.RecordDossier(identity, {
            .id = 0x5678,
            .kind = ObservedTargetKind::Planet,
            .displayName = "Jemison",
        });
        inbox.RecordLifecycle(false);
        const auto observations = inbox.Drain();

        Require(observations.lifecycleCount == 1, "close was not recorded");
        Require(!observations.lifecycle[0].opening, "close was recorded as an open");
        Require(observations.lifecycle[0].identity == identity, "close lost the open identity");
        Require(!observations.mapData, "close retained stale map data");
        Require(!observations.markers, "close retained stale markers");
        Require(!observations.dossier, "close retained a stale dossier");
    }

    void TestMapDataCarriesCurrentBody()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordMapData(identity, MapView::System, 0x1234, 0xABCD);

        const auto observations = inbox.Drain();
        Require(observations.mapData.has_value(), "map data was not recorded");
        Require(observations.mapData->identity == identity, "map data retained the wrong identity");
        Require(observations.mapData->view == MapView::System, "map data retained the wrong view");
        Require(observations.mapData->currentBodyId == 0x1234, "map data retained the wrong current body");
        Require(observations.mapData->currentSystemFormId == 0xABCD, "map data retained the wrong current-system form");
    }

    void TestMapDataCoalescesFailClosed()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordMapData(identity, MapView::System, 0x1234, 0xABCD);
        inbox.RecordMapData(identity, MapView::Galaxy, 0x5678, 0xBCDE);

        const auto observations = inbox.Drain();
        Require(observations.mapData.has_value(), "map data was not recorded");
        Require(observations.mapData->view == MapView::Unknown, "conflicting views did not become unknown");
        Require(observations.mapData->currentBodyId == 0, "conflicting current bodies did not become unavailable");
        Require(observations.mapData->currentSystemFormId == 0, "conflicting current-system forms did not become unavailable");
    }

    void TestTargetObservationsCarryPlainValues()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordMarkers(identity, {
            .highlightedCount = 1,
            .highlighted = {
                .id = 0x1234,
                .kind = ObservedTargetKind::Moon,
                .displayName = "Luna",
            },
        });
        inbox.RecordDossier(identity, {
            .id = 0x1234,
            .kind = ObservedTargetKind::Moon,
            .displayName = "Luna",
        });

        const auto observations = inbox.Drain();
        Require(observations.markers.has_value(), "markers were not recorded");
        Require(observations.markers->identity == identity, "markers retained the wrong identity");
        Require(observations.markers->update.highlightedCount == 1, "markers retained the wrong highlight count");
        Require(observations.markers->update.highlighted.id == 0x1234, "markers retained the wrong body");
        Require(observations.markers->update.highlighted.kind == ObservedTargetKind::Moon, "markers retained the wrong body kind");
        Require(observations.markers->update.highlighted.displayName == "Luna", "markers retained the wrong name");

        Require(observations.dossier.has_value(), "dossier was not recorded");
        Require(observations.dossier->identity == identity, "dossier retained the wrong identity");
        Require(observations.dossier->target.id == 0x1234, "dossier retained the wrong body");
        Require(observations.dossier->target.kind == ObservedTargetKind::Moon, "dossier retained the wrong body kind");
        Require(observations.dossier->target.displayName == "Luna", "dossier retained the wrong name");
    }

    void TestLatestTargetObservationsReplaceStaleValues()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordMarkers(identity, {
            .highlightedCount = 1,
            .highlighted = {
                .id = 0x1234,
                .kind = ObservedTargetKind::Planet,
                .displayName = "Jemison",
            },
        });
        inbox.RecordMarkers(identity, {});
        inbox.RecordDossier(identity, {
            .id = 0x1234,
            .kind = ObservedTargetKind::Planet,
            .displayName = "Jemison",
        });
        inbox.RecordDossier(identity, {});

        const auto observations = inbox.Drain();
        Require(observations.markers.has_value(), "latest markers were not recorded");
        Require(observations.markers->update.highlightedCount == 0, "latest markers retained a stale highlight");
        Require(observations.markers->update.highlighted.id == 0, "latest markers retained a stale body");
        Require(observations.dossier.has_value(), "latest dossier was not recorded");
        Require(observations.dossier->target.id == 0, "latest dossier retained a stale body");
    }

    void TestOldMovieMapDataIsRejected()
    {
        MapObservationInbox inbox;
        const auto oldIdentity = OpenMap(inbox);

        inbox.RecordMovieCreated(200);
        inbox.RecordMapData(oldIdentity, MapView::System, 0x1234, 0xABCD);
        inbox.RecordMarkers(oldIdentity, {
            .highlightedCount = 1,
            .highlighted = {
                .id = 0x5678,
                .kind = ObservedTargetKind::Planet,
            },
        });
        inbox.RecordDossier(oldIdentity, {
            .id = 0x5678,
            .kind = ObservedTargetKind::Planet,
        });

        const auto observations = inbox.Drain();
        Require(observations.movieGeneration == 2, "replacement movie retained the wrong generation");
        Require(!observations.mapData, "old movie published map data into the replacement");
        Require(!observations.markers, "old movie published markers into the replacement");
        Require(!observations.dossier, "old movie published a dossier into the replacement");
    }

    void TestOldSessionTargetObservationsAreRejected()
    {
        MapObservationInbox inbox;
        const auto oldIdentity = OpenMap(inbox);

        inbox.RecordLifecycle(false);
        inbox.RecordLifecycle(true);
        const auto lifecycle = inbox.Drain();
        Require(lifecycle.lifecycleCount == 2, "reopen lifecycle was not recorded");
        const auto newIdentity = lifecycle.lifecycle[1].identity;
        Require(newIdentity.IsValid() && newIdentity != oldIdentity, "reopen did not create a new identity");

        inbox.RecordMarkers(oldIdentity, {
            .highlightedCount = 1,
            .highlighted = {
                .id = 0x1234,
                .kind = ObservedTargetKind::Planet,
            },
        });
        inbox.RecordDossier(oldIdentity, {
            .id = 0x1234,
            .kind = ObservedTargetKind::Planet,
        });

        const auto stale = inbox.Drain();
        Require(!stale.markers, "old session published markers into the reopened map");
        Require(!stale.dossier, "old session published a dossier into the reopened map");
    }

    void TestActionsCarryIdentityAndFailClosedOnConflict()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordAction(identity, MapObservationInbox::Action::Tap);
        const auto tap = inbox.Drain();

        Require(tap.action.has_value(), "map action was not recorded");
        Require(tap.action->identity == identity, "map action retained the wrong identity");
        Require(tap.action->action == MapObservationInbox::Action::Tap, "map action retained the wrong gesture");
        Require(!tap.actionOverflowed, "single map action reported overflow");

        inbox.RecordAction(identity, MapObservationInbox::Action::Tap);
        inbox.RecordAction(identity, MapObservationInbox::Action::HoldCompleted);
        const auto conflict = inbox.Drain();

        Require(!conflict.action, "conflicting map actions retained an arbitrary gesture");
        Require(conflict.actionOverflowed, "conflicting map actions did not fail closed");
    }

    void TestStaleActionsAreRejectedAndLifecycleClearsPendingAction()
    {
        MapObservationInbox inbox;
        const auto oldIdentity = OpenMap(inbox);

        inbox.RecordAction(oldIdentity, MapObservationInbox::Action::Tap);
        inbox.RecordLifecycle(false);
        inbox.RecordAction(oldIdentity, MapObservationInbox::Action::Tap);
        const auto closed = inbox.Drain();
        Require(!closed.action, "closed session accepted a map action");

        inbox.RecordLifecycle(true);
        const auto reopened = inbox.Drain();
        const auto newIdentity = reopened.lifecycle[0].identity;
        inbox.RecordAction(oldIdentity, MapObservationInbox::Action::Tap);
        Require(!inbox.Drain().action, "old session published an action into the reopened map");

        inbox.RecordAction(newIdentity, MapObservationInbox::Action::Tap);
        inbox.RecordMovieCreated(200);
        const auto replaced = inbox.Drain();
        Require(!replaced.action, "movie replacement retained a pending map action");
        Require(!replaced.actionOverflowed, "movie replacement retained action overflow state");
    }

    void TestOverflowDropsPartialHistory()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordMapData(identity, MapView::System, 0x1234, 0xABCD);
        inbox.RecordMarkers(identity, {
            .highlightedCount = 1,
            .highlighted = {
                .id = 0x5678,
                .kind = ObservedTargetKind::Planet,
            },
        });
        inbox.RecordDossier(identity, {
            .id = 0x5678,
            .kind = ObservedTargetKind::Planet,
        });
        inbox.RecordAction(identity, MapObservationInbox::Action::Tap);

        for (std::size_t index = 0; index <= MapObservationInbox::MaxLifecycleObservations; ++index) {
            inbox.RecordLifecycle(true);
        }

        const auto observations = inbox.Drain();
        Require(observations.lifecycleOverflowed, "full lifecycle queue did not report overflow");
        Require(observations.lifecycleCount == 0, "overflow retained partial lifecycle history");
        Require(!observations.mapData, "overflow retained stale map data");
        Require(!observations.markers, "overflow retained stale markers");
        Require(!observations.dossier, "overflow retained a stale dossier");
        Require(!observations.action, "overflow retained a stale map action");
        Require(!observations.actionOverflowed, "lifecycle overflow retained action overflow state");
    }

    void RunTests()
    {
        TestCloseKeepsOpenIdentity();
        TestMapDataCarriesCurrentBody();
        TestMapDataCoalescesFailClosed();
        TestTargetObservationsCarryPlainValues();
        TestLatestTargetObservationsReplaceStaleValues();
        TestOldMovieMapDataIsRejected();
        TestOldSessionTargetObservationsAreRejected();
        TestActionsCarryIdentityAndFailClosedOnConflict();
        TestStaleActionsAreRejectedAndLifecycleClearsPendingAction();
        TestOverflowDropsPartialHistory();
    }
}

void RunMapObservationInboxTests()
{
    RunTests();
}
