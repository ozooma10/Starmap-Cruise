#include "Bodies/BodyCatalog.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr ::FormID JemisonId = 0x10;
    constexpr ::FormID MarsId = 0x20;
    constexpr ::FormID AlphaCentauriId = 0x100;
    constexpr ::FormID SolId = 0x200;

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error{ std::string{ message } };
    }

    std::vector<::IndexedBodyObservation> ValidBodies()
    {
        return {
            {
                .id = JemisonId,
                .systemId = AlphaCentauriId,
            },
            {
                .id = MarsId,
                .systemId = SolId,
            },
        };
    }

    void TestCatalogStartsEmpty()
    {
        ::BodyCatalog catalog;

        Require(catalog.Status() == ::BodyCatalogStatus::Empty,
            "new catalog did not start Empty");
        Require(!catalog.IsReady(),
            "new catalog was reported as ready");
        Require(catalog.CurrentGeneration() == 0,
            "new catalog started with a valid generation");
        Require(!catalog.Find(JemisonId),
            "empty catalog returned a body");
    }

    void TestValidPublicationBecomesReady()
    {
        ::BodyCatalog catalog;
        const auto generation = catalog.BeginLoad();

        Require(generation != 0,
            "BeginLoad returned the invalid generation");
        Require(catalog.Status() == ::BodyCatalogStatus::Loading,
            "BeginLoad did not enter Loading");
        Require(!catalog.IsReady(),
            "loading catalog was reported as ready");

        Require(catalog.Publish(generation, ValidBodies()),
            "valid body publication was rejected");
        Require(catalog.Status() == ::BodyCatalogStatus::Ready,
            "valid publication did not enter Ready");
        Require(catalog.IsReady(),
            "valid publication was not reported as ready");

        const auto jemison = catalog.Find(JemisonId);
        Require(jemison.has_value(),
            "published body could not be found");
        Require(jemison->id == JemisonId,
            "published body retained the wrong identity");
        Require(jemison->systemId == AlphaCentauriId,
            "published body retained the wrong system identity");

        const auto mars = catalog.Find(MarsId);
        Require(mars.has_value() && mars->systemId == SolId,
            "second published body could not be found");
        Require(!catalog.Find(0xDEADBEEF),
            "catalog returned an unpublished body");
    }

    void TestEmptyPublicationIsReady()
    {
        ::BodyCatalog catalog;
        const auto generation = catalog.BeginLoad();

        Require(catalog.Publish(generation, {}),
            "empty successful load was rejected");
        Require(catalog.IsReady(),
            "empty successful load was not reported as ready");
        Require(catalog.Status() == ::BodyCatalogStatus::Ready,
            "empty successful load did not enter Ready");
        Require(!catalog.Find(JemisonId),
            "empty successful load returned a body");
    }

    void TestInvalidBodyFailsWholePublication()
    {
        ::BodyCatalog catalog;
        const auto generation = catalog.BeginLoad();

        auto bodies = ValidBodies();
        bodies.push_back({
            .id = 0,
            .systemId = AlphaCentauriId,
        });

        Require(!catalog.Publish(generation, std::move(bodies)),
            "zero body identity was accepted");
        Require(catalog.Status() == ::BodyCatalogStatus::Failed,
            "invalid body did not fail the catalog load");
        Require(!catalog.IsReady(),
            "invalid body left the catalog ready");
        Require(!catalog.Find(JemisonId),
            "invalid publication leaked an earlier valid body");

        const auto retryGeneration = catalog.BeginLoad();
        Require(!catalog.Publish(retryGeneration, {
            {
                .id = JemisonId,
                .systemId = 0,
            },
        }), "zero system identity was accepted");
        Require(catalog.Status() == ::BodyCatalogStatus::Failed,
            "zero system identity did not fail the load");
    }

    void TestDuplicateBodyFailsWholePublication()
    {
        ::BodyCatalog catalog;
        const auto generation = catalog.BeginLoad();

        Require(!catalog.Publish(generation, {
            {
                .id = JemisonId,
                .systemId = AlphaCentauriId,
            },
            {
                .id = JemisonId,
                .systemId = SolId,
            },
        }), "duplicate body identity was accepted");

        Require(catalog.Status() == ::BodyCatalogStatus::Failed,
            "duplicate body did not fail the catalog load");
        Require(!catalog.Find(JemisonId),
            "duplicate publication leaked a body");
    }

    void TestExplicitFailureClearsLoadingCatalog()
    {
        ::BodyCatalog catalog;
        const auto generation = catalog.BeginLoad();

        Require(catalog.Fail(generation),
            "current loading generation could not fail");
        Require(catalog.Status() == ::BodyCatalogStatus::Failed,
            "explicit failure did not enter Failed");
        Require(!catalog.IsReady(),
            "failed catalog was reported as ready");
        Require(!catalog.Fail(generation),
            "already failed generation accepted another failure");
        Require(!catalog.Publish(generation, ValidBodies()),
            "already failed generation accepted a publication");
    }

    void TestNewLoadRejectsStaleCompletion()
    {
        ::BodyCatalog catalog;
        const auto oldGeneration = catalog.BeginLoad();
        const auto currentGeneration = catalog.BeginLoad();

        Require(currentGeneration != oldGeneration,
            "new load reused the prior generation");
        Require(!catalog.Publish(oldGeneration, ValidBodies()),
            "stale load publication was accepted");
        Require(!catalog.Fail(oldGeneration),
            "stale load failure was accepted");
        Require(catalog.Status() == ::BodyCatalogStatus::Loading,
            "stale completion changed the current load state");
        Require(catalog.CurrentGeneration() == currentGeneration,
            "stale completion changed the current generation");
        Require(!catalog.Find(JemisonId),
            "stale publication leaked a body");

        Require(catalog.Publish(currentGeneration, ValidBodies()),
            "current load publication was rejected after stale completion");
        Require(catalog.IsReady(),
            "current load did not become ready");
    }

    void TestClearInvalidatesOutstandingLoad()
    {
        ::BodyCatalog catalog;
        const auto generation = catalog.BeginLoad();

        catalog.Clear();

        Require(catalog.Status() == ::BodyCatalogStatus::Empty,
            "Clear did not return the catalog to Empty");
        Require(catalog.CurrentGeneration() != generation,
            "Clear did not invalidate the outstanding generation");
        Require(!catalog.Publish(generation, ValidBodies()),
            "cleared generation published late results");
        Require(catalog.Status() == ::BodyCatalogStatus::Empty,
            "late publication changed the cleared catalog");
        Require(!catalog.Find(JemisonId),
            "cleared catalog retained a body");
    }

    void TestReadyCatalogCannotBeRepublished()
    {
        ::BodyCatalog catalog;
        const auto generation = catalog.BeginLoad();
        Require(catalog.Publish(generation, ValidBodies()),
            "initial publication was rejected");

        Require(!catalog.Publish(generation, {
            {
                .id = 0x30,
                .systemId = SolId,
            },
        }), "ready generation accepted a second publication");

        Require(catalog.Find(JemisonId).has_value(),
            "rejected second publication replaced the ready catalog");
        Require(!catalog.Find(0x30),
            "rejected second publication leaked a body");
    }

    void RunTests()
    {
        TestCatalogStartsEmpty();
        TestValidPublicationBecomesReady();
        TestEmptyPublicationIsReady();
        TestInvalidBodyFailsWholePublication();
        TestDuplicateBodyFailsWholePublication();
        TestExplicitFailureClearsLoadingCatalog();
        TestNewLoadRejectsStaleCompletion();
        TestClearInvalidatesOutstandingLoad();
        TestReadyCatalogCannotBeRepublished();
    }
}

void RunBodyCatalogTests()
{
    RunTests();
}
