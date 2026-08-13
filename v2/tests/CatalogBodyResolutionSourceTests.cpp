#include "Application/BodyResolver.h"
#include "Bodies/CatalogBodyResolutionSource.h"
#include "Starfield/StarfieldLiveBodyProbe.h"
#include "TestSuites.h"

#include <concepts>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

static_assert(std::derived_from<
    ::StarfieldLiveBodyProbe,
    ::LiveBodyProbe>);
static_assert(!std::is_abstract_v<::StarfieldLiveBodyProbe>);

namespace
{
    constexpr ::FormID JemisonId = 0x10;
    constexpr ::FormID MarsId = 0x20;
    constexpr ::FormID AlphaCentauriId = 0x100;

    class FakeLiveBodyProbe final : public ::LiveBodyProbe
    {
    public:
        bool IsLiveBody(::FormID bodyId) const override
        {
            ++calls;
            lastBodyId = bodyId;
            return live;
        }

        bool live{ true };
        mutable std::size_t calls{ 0 };
        mutable ::FormID lastBodyId{ 0 };
    };

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error{ std::string{ message } };
    }

    ::TargetObservation Jemison()
    {
        return {
            .id = JemisonId,
            .kind = ::ObservedTargetKind::Planet,
            .displayName = "Jemison",
        };
    }

    void PublishJemison(::BodyCatalog& catalog)
    {
        const auto generation = catalog.BeginLoad();

        Require(catalog.Publish(generation, {
            {
                .id = JemisonId,
                .systemId = AlphaCentauriId,
            },
        }), "test catalog could not publish Jemison");
    }

    void TestSourceDelegatesLiveBodyIdentity()
    {
        FakeLiveBodyProbe probe;
        ::BodyCatalog catalog;
        ::CatalogBodyResolutionSource source{ probe, catalog };

        Require(source.IsLiveBody(MarsId),
            "source lost the live-body result");
        Require(probe.calls == 1,
            "source did not query the live-body probe exactly once");
        Require(probe.lastBodyId == MarsId,
            "source queried the live-body probe with the wrong identity");

        probe.live = false;

        Require(!source.IsLiveBody(JemisonId),
            "source lost a negative live-body result");
        Require(probe.calls == 2 && probe.lastBodyId == JemisonId,
            "source did not delegate the second live-body identity");
    }

    void TestSourceReflectsCatalogStateAndEntries()
    {
        FakeLiveBodyProbe probe;
        ::BodyCatalog catalog;
        ::CatalogBodyResolutionSource source{ probe, catalog };

        Require(!source.IsBodyIndexReady(),
            "empty catalog was reported as ready");
        Require(!source.FindIndexedBody(JemisonId),
            "empty catalog returned an indexed body");

        const auto generation = catalog.BeginLoad();

        Require(!source.IsBodyIndexReady(),
            "loading catalog was reported as ready");
        Require(!source.FindIndexedBody(JemisonId),
            "loading catalog returned an indexed body");

        Require(catalog.Publish(generation, {
            {
                .id = JemisonId,
                .systemId = AlphaCentauriId,
            },
        }), "test catalog publication failed");

        Require(source.IsBodyIndexReady(),
            "source did not observe the catalog becoming ready");

        const auto indexedBody = source.FindIndexedBody(JemisonId);
        Require(indexedBody.has_value(),
            "source did not return the published body");
        Require(indexedBody->id == JemisonId,
            "source returned the wrong body identity");
        Require(indexedBody->systemId == AlphaCentauriId,
            "source returned the wrong system identity");
        Require(!source.FindIndexedBody(MarsId),
            "source returned an unpublished body");
    }

    void TestResolverReportsLoadingCatalog()
    {
        FakeLiveBodyProbe probe;
        ::BodyCatalog catalog;
        catalog.BeginLoad();
        ::CatalogBodyResolutionSource source{ probe, catalog };
        ::BodyResolver resolver{ source };

        const auto result = resolver.Resolve(Jemison());

        Require(result.dossierId == JemisonId,
            "loading resolution lost the dossier identity");
        Require(result.dossierIsLiveBody,
            "loading resolution lost live-form evidence");
        Require(!result.bodyIndexReady,
            "loading catalog was reported as ready");
        Require(!result.indexedBody,
            "loading catalog produced an indexed body");
        Require(probe.calls == 1 && probe.lastBodyId == JemisonId,
            "loading resolution queried the wrong live body");
    }

    void TestResolverKeepsNonLiveEvidenceSeparate()
    {
        FakeLiveBodyProbe probe;
        probe.live = false;
        ::BodyCatalog catalog;
        PublishJemison(catalog);
        ::CatalogBodyResolutionSource source{ probe, catalog };
        ::BodyResolver resolver{ source };

        const auto result = resolver.Resolve(Jemison());

        Require(!result.dossierIsLiveBody,
            "non-live body was reported as live");
        Require(result.bodyIndexReady,
            "ready catalog evidence was lost for a non-live body");
        Require(!result.indexedBody,
            "non-live body produced an indexed resolution");
    }

    void TestResolverReportsReadyButMissingBody()
    {
        FakeLiveBodyProbe probe;
        ::BodyCatalog catalog;
        const auto generation = catalog.BeginLoad();
        Require(catalog.Publish(generation, {}),
            "test catalog could not publish an empty result");
        ::CatalogBodyResolutionSource source{ probe, catalog };
        ::BodyResolver resolver{ source };

        const auto result = resolver.Resolve(Jemison());

        Require(result.dossierIsLiveBody,
            "missing indexed body lost live-form evidence");
        Require(result.bodyIndexReady,
            "ready empty catalog was reported as loading");
        Require(!result.indexedBody,
            "ready empty catalog produced an indexed body");
    }

    void TestResolverProducesCompleteCatalogResolution()
    {
        FakeLiveBodyProbe probe;
        ::BodyCatalog catalog;
        PublishJemison(catalog);
        ::CatalogBodyResolutionSource source{ probe, catalog };
        ::BodyResolver resolver{ source };

        const auto result = resolver.Resolve(Jemison());

        Require(result.dossierId == JemisonId,
            "complete resolution lost the dossier identity");
        Require(result.dossierIsLiveBody,
            "complete resolution lost live-form evidence");
        Require(result.bodyIndexReady,
            "complete resolution lost catalog readiness");
        Require(result.indexedBody.has_value(),
            "complete resolution did not return the catalog body");
        Require(result.indexedBody->id == JemisonId,
            "complete resolution returned the wrong body identity");
        Require(result.indexedBody->systemId == AlphaCentauriId,
            "complete resolution returned the wrong system identity");
    }

    void RunTests()
    {
        TestSourceDelegatesLiveBodyIdentity();
        TestSourceReflectsCatalogStateAndEntries();
        TestResolverReportsLoadingCatalog();
        TestResolverKeepsNonLiveEvidenceSeparate();
        TestResolverReportsReadyButMissingBody();
        TestResolverProducesCompleteCatalogResolution();
    }
}

void RunCatalogBodyResolutionSourceTests()
{
    RunTests();
}
