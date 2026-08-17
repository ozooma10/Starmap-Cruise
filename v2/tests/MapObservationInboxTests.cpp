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

        inbox.RecordLifecycle(false);
        const auto observations = inbox.Drain();

        Require(observations.lifecycleCount == 1, "close was not recorded");
        Require(!observations.lifecycle[0].opening, "close was recorded as an open");
        Require(observations.lifecycle[0].identity == identity, "close lost the open identity");
        Require(!observations.mapData, "close retained stale map data");
    }

    void TestMapDataCarriesCurrentBody()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordMapData(identity, MapView::System, 0x1234);

        const auto observations = inbox.Drain();
        Require(observations.mapData.has_value(), "map data was not recorded");
        Require(observations.mapData->identity == identity, "map data retained the wrong identity");
        Require(observations.mapData->view == MapView::System, "map data retained the wrong view");
        Require(observations.mapData->currentBodyId == 0x1234, "map data retained the wrong current body");
    }

    void TestMapDataCoalescesFailClosed()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordMapData(identity, MapView::System, 0x1234);
        inbox.RecordMapData(identity, MapView::Galaxy, 0x5678);

        const auto observations = inbox.Drain();
        Require(observations.mapData.has_value(), "map data was not recorded");
        Require(observations.mapData->view == MapView::Unknown, "conflicting views did not become unknown");
        Require(observations.mapData->currentBodyId == 0, "conflicting current bodies did not become unavailable");
    }

    void TestOldMovieMapDataIsRejected()
    {
        MapObservationInbox inbox;
        const auto oldIdentity = OpenMap(inbox);

        inbox.RecordMovieCreated(200);
        inbox.RecordMapData(oldIdentity, MapView::System, 0x1234);

        const auto observations = inbox.Drain();
        Require(observations.movieGeneration == 2, "replacement movie retained the wrong generation");
        Require(!observations.mapData, "old movie published map data into the replacement");
    }

    void TestOverflowDropsPartialHistory()
    {
        MapObservationInbox inbox;

        for (std::size_t index = 0; index <= MapObservationInbox::MaxLifecycleObservations; ++index) {
            inbox.RecordLifecycle(true);
        }

        const auto observations = inbox.Drain();
        Require(observations.lifecycleOverflowed, "full lifecycle queue did not report overflow");
        Require(observations.lifecycleCount == 0, "overflow retained partial lifecycle history");
        Require(!observations.mapData, "overflow retained stale map data");
    }

    void RunTests()
    {
        TestCloseKeepsOpenIdentity();
        TestMapDataCarriesCurrentBody();
        TestMapDataCoalescesFailClosed();
        TestOldMovieMapDataIsRejected();
        TestOverflowDropsPartialHistory();
    }
}

void RunMapObservationInboxTests()
{
    RunTests();
}
