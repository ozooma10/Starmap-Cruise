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
    }

    void TestViewsCoalesceFailClosed()
    {
        MapObservationInbox inbox;
        const auto identity = OpenMap(inbox);

        inbox.RecordView(identity, MapView::System);
        inbox.RecordView(identity, MapView::Galaxy);

        const auto observations = inbox.Drain();
        Require(observations.view.has_value(), "view was not recorded");
        Require(observations.view->view == MapView::Unknown, "conflicting views did not become unknown");
    }

    void TestOldMovieViewIsRejected()
    {
        MapObservationInbox inbox;
        const auto oldIdentity = OpenMap(inbox);

        inbox.RecordMovieCreated(200);
        inbox.RecordView(oldIdentity, MapView::System);

        const auto observations = inbox.Drain();
        Require(observations.movieGeneration == 2, "replacement movie retained the wrong generation");
        Require(!observations.view, "old movie published a view into the replacement");
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
    }

    void RunTests()
    {
        TestCloseKeepsOpenIdentity();
        TestViewsCoalesceFailClosed();
        TestOldMovieViewIsRejected();
        TestOverflowDropsPartialHistory();
    }
}

void RunMapObservationInboxTests()
{
    RunTests();
}
