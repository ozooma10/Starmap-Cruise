#include "Application/BodyResolver.h"
#include "TestSuites.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    constexpr ::FormID JemisonId = 0x10;
    constexpr ::FormID AlphaCentauriId = 0x100;

    class FakeBodyResolutionSource final : public ::BodyResolutionSource
    {
    public:
        bool IsLiveBody(::FormID bodyId) const override
        {
            ++liveBodyCalls;
            lastLiveBodyId = bodyId;
            return liveBody;
        }

        bool IsBodyIndexReady() const override
        {
            ++indexReadyCalls;
            return indexReady;
        }

        std::optional<::IndexedBodyObservation> FindIndexedBody(::FormID bodyId) const override
        {
            ++findBodyCalls;
            lastFindBodyId = bodyId;
            return indexedBody;
        }

        bool liveBody {false};
        bool indexReady {false};
        std::optional<::IndexedBodyObservation> indexedBody;

        mutable std::size_t liveBodyCalls {0};
        mutable std::size_t indexReadyCalls {0};
        mutable std::size_t findBodyCalls {0};
        mutable ::FormID lastLiveBodyId {0};
        mutable ::FormID lastFindBodyId {0};
    };

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    ::TargetObservation Jemison()
    {
        return {
            .id = JemisonId,
            .kind = ::ObservedTargetKind::Planet,
            .displayName = "Jemison",
        };
    }

    void RequireNoSourceCalls(const FakeBodyResolutionSource& source, std::string_view message)
    {
        Require(source.liveBodyCalls == 0, message);
        Require(source.indexReadyCalls == 0, message);
        Require(source.findBodyCalls == 0, message);
    }

    void TestInvalidDossierDoesNotQuerySource()
    {
        FakeBodyResolutionSource source;
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve({
            .kind = ::ObservedTargetKind::Planet,
        });

        Require(result.dossierId == 0, "invalid dossier produced a nonzero identity");
        Require(!result.dossierIsLiveBody, "invalid dossier was reported as live");
        Require(!result.bodyIndexReady, "invalid dossier reported index readiness");
        Require(!result.indexedBody, "invalid dossier produced an indexed body");
        RequireNoSourceCalls(source, "invalid dossier queried the resolution source");
    }

    void TestUnsupportedDossierDoesNotQuerySource()
    {
        FakeBodyResolutionSource source;
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve({
            .id = JemisonId,
            .kind = ::ObservedTargetKind::Unsupported,
            .displayName = "Unsupported",
        });

        Require(result.dossierId == JemisonId, "unsupported dossier lost its identity");
        Require(!result.dossierIsLiveBody, "unsupported dossier was reported as live");
        Require(!result.bodyIndexReady, "unsupported dossier reported index readiness");
        Require(!result.indexedBody, "unsupported dossier produced an indexed body");
        RequireNoSourceCalls(source, "unsupported dossier queried the resolution source");
    }

    void TestNonLiveBodyDoesNotQueryIndexEntry()
    {
        FakeBodyResolutionSource source;
        source.indexReady = true;
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve(Jemison());

        Require(result.dossierId == JemisonId, "non-live result lost the dossier identity");
        Require(!result.dossierIsLiveBody, "non-live body was reported as live");
        Require(result.bodyIndexReady, "independent index readiness was not captured");
        Require(!result.indexedBody, "non-live body produced an indexed observation");
        Require(source.liveBodyCalls == 1 && source.lastLiveBodyId == JemisonId, "live-form lookup did not receive the dossier identity");
        Require(source.indexReadyCalls == 1, "index readiness was not captured exactly once");
        Require(source.findBodyCalls == 0, "non-live body queried the index entry");
    }

    void TestLoadingIndexDoesNotQueryEntry()
    {
        FakeBodyResolutionSource source;
        source.liveBody = true;
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve(Jemison());

        Require(result.dossierIsLiveBody, "live body was not reported as live");
        Require(!result.bodyIndexReady, "loading index was reported as ready");
        Require(!result.indexedBody, "loading index produced an indexed observation");
        Require(source.findBodyCalls == 0, "loading index was queried for an entry");
    }

    void TestReadyIndexCanReportMissingBody()
    {
        FakeBodyResolutionSource source;
        source.liveBody = true;
        source.indexReady = true;
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve(Jemison());

        Require(result.dossierIsLiveBody, "ready missing body lost live-form evidence");
        Require(result.bodyIndexReady, "ready missing body lost index readiness");
        Require(!result.indexedBody, "missing body produced an indexed observation");
        Require(source.findBodyCalls == 1 && source.lastFindBodyId == JemisonId, "ready index was not queried with the dossier identity");
    }

    void TestValidIndexedMoonProducesCompleteResolution()
    {
        FakeBodyResolutionSource source;
        source.liveBody = true;
        source.indexReady = true;
        source.indexedBody = ::IndexedBodyObservation {
            .id = JemisonId,
            .systemId = AlphaCentauriId,
        };
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve({
            .id = JemisonId,
            .kind = ::ObservedTargetKind::Moon,
            .displayName = "Moon",
        });

        Require(result.dossierId == JemisonId, "complete resolution lost the dossier identity");
        Require(result.dossierIsLiveBody, "complete resolution lost live-form evidence");
        Require(result.bodyIndexReady, "complete resolution lost index readiness");
        Require(result.indexedBody.has_value(), "valid indexed body produced no observation");
        Require(result.indexedBody->id == JemisonId, "complete resolution retained the wrong body identity");
        Require(result.indexedBody->systemId == AlphaCentauriId, "complete resolution retained the wrong system identity");
        Require(source.findBodyCalls == 1 && source.lastFindBodyId == JemisonId, "complete resolution queried the wrong index identity");
    }

    void RunTests()
    {
        TestInvalidDossierDoesNotQuerySource();
        TestUnsupportedDossierDoesNotQuerySource();
        TestNonLiveBodyDoesNotQueryIndexEntry();
        TestLoadingIndexDoesNotQueryEntry();
        TestReadyIndexCanReportMissingBody();
        TestValidIndexedMoonProducesCompleteResolution();
    }
}

void RunBodyResolverTests()
{
    RunTests();
}
