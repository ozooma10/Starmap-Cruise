#include "Starfield/TravelObservationInbox.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error {std::string {message}};
        }
    }

    void TestMixedProducersRetainOneOrderedValueBatch()
    {
        TravelObservationInbox inbox;
        inbox.RecordGravJump(0, 0x1000);
        inbox.RecordLoadingMenu(true);
        inbox.RecordGravJump(1, 0x1000);
        inbox.RecordLoadingMenu(false);
        inbox.RecordGravJump(2, 0x1000);
        inbox.RecordLoadGame();

        const auto observations = inbox.Drain();
        Require(!observations.overflowed, "bounded mixed travel batch overflowed");
        Require(observations.count == 6, "mixed travel batch retained the wrong count");
        for (std::size_t index = 0; index < observations.count; ++index) {
            Require(observations.values[index].sequence != 0, "travel observation received a zero sequence");
            Require(observations.values[index].ticks != 0, "travel observation received a zero timestamp");
            if (index != 0) {
                Require(observations.values[index - 1].sequence < observations.values[index].sequence, "travel observations were not reduced in producer order");
            }
        }
        Require(observations.values[0].kind == TravelObservationInbox::Kind::GravJump && observations.values[0].gravState == 0 && observations.values[0].destinationId == 0x1000, "first grav-jump observation was corrupted");
        Require(observations.values[1].kind == TravelObservationInbox::Kind::LoadingMenu && observations.values[1].opening, "LoadingMenu open observation was corrupted");
        Require(observations.values[5].kind == TravelObservationInbox::Kind::LoadGame, "load-game observation was corrupted");
        Require(inbox.Drain().count == 0, "travel observations were delivered more than once");
    }

    void TestOverflowDropsTheWholeBatchAndRecovers()
    {
        TravelObservationInbox inbox;
        for (std::size_t index = 0; index <= TravelObservationInbox::MaxObservations; ++index) {
            inbox.RecordGravJump(static_cast<std::uint32_t>(index % 3), 0x2000);
        }
        inbox.RecordLoadGame();

        const auto overflow = inbox.Drain();
        Require(overflow.overflowed, "travel inbox did not report its fixed bound");
        Require(overflow.count == 0, "travel inbox exposed a partial batch after overflow");

        inbox.RecordLoadingMenu(true);
        const auto recovered = inbox.Drain();
        Require(!recovered.overflowed && recovered.count == 1, "travel inbox did not recover after the overflow was drained");
        Require(recovered.values[0].kind == TravelObservationInbox::Kind::LoadingMenu && recovered.values[0].opening, "post-overflow observation was corrupted");
    }

    void TestExactCapacityAndConcurrentProducersRemainOrdered()
    {
        TravelObservationInbox inbox;
        constexpr std::size_t ProducerCount = 4;
        constexpr std::size_t ValuesPerProducer = TravelObservationInbox::MaxObservations / ProducerCount;

        std::vector<std::thread> producers;
        for (std::size_t producer = 0; producer < ProducerCount; ++producer) {
            producers.emplace_back([&inbox, producer] {
                for (std::size_t index = 0; index < ValuesPerProducer; ++index) {
                    inbox.RecordGravJump(static_cast<std::uint32_t>(index % 3), static_cast<FormID>(0x3000 + producer));
                }
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }

        const auto observations = inbox.Drain();
        Require(!observations.overflowed, "exact-capacity concurrent batch overflowed");
        Require(observations.count == TravelObservationInbox::MaxObservations, "concurrent batch lost an observation");
        for (std::size_t index = 1; index < observations.count; ++index) {
            Require(observations.values[index - 1].sequence < observations.values[index].sequence, "concurrent observations were not serialized by sequence");
        }

        Require(inbox.Drain().count == 0, "drained concurrent batch was delivered twice");
    }
}

void RunTravelObservationInboxTests()
{
    TestMixedProducersRetainOneOrderedValueBatch();
    TestOverflowDropsTheWholeBatchAndRecovers();
    TestExactCapacityAndConcurrentProducersRemainOrdered();
}
