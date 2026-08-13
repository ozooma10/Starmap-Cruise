#include "Application/BodyResolver.h"
#include "Starfield/StarfieldBodyResolutionSource.h"
#include "TestSuites.h"

#include <concepts>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

static_assert(std::derived_from<::StarfieldBodyResolutionSource, ::BodyResolutionSource>);
static_assert(!std::is_abstract_v<::StarfieldBodyResolutionSource>);

namespace
{
    constexpr ::FormID JemisonId = 0x10;
    constexpr ::FormID AlphaCentauriId = 0x100;

    class FakeBodyResolutionSource final : public ::BodyResolutionSource
    {
    public:
        ::BodyLookupResult ResolveBody(::FormID bodyId) const override
        {
            ++calls;
            lastBodyId = bodyId;
            return result;
        }

        ::BodyLookupResult result;
        mutable std::size_t calls {0};
        mutable ::FormID lastBodyId {0};
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

    void TestInvalidDossierDoesNotQuerySource()
    {
        FakeBodyResolutionSource source;
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve({
            .kind = ::ObservedTargetKind::Planet,
        });

        Require(result.dossierId == 0, "invalid dossier produced a nonzero identity");
        Require(!result.dossierIsLiveBody, "invalid dossier was reported as live");
        Require(!result.resolvedBody, "invalid dossier produced a resolved body");
        Require(source.calls == 0, "invalid dossier queried the resolution source");
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
        Require(!result.resolvedBody, "unsupported dossier produced a resolved body");
        Require(source.calls == 0, "unsupported dossier queried the resolution source");
    }

    void TestNonLiveBodyFailsClosed()
    {
        FakeBodyResolutionSource source;
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve(Jemison());

        Require(result.dossierId == JemisonId, "non-live result lost the dossier identity");
        Require(!result.dossierIsLiveBody, "non-live body was reported as live");
        Require(!result.resolvedBody, "non-live body produced a resolved body");
        Require(source.calls == 1 && source.lastBodyId == JemisonId, "body lookup received the wrong dossier identity");
    }

    void TestLiveBodyCanLackSystemData()
    {
        FakeBodyResolutionSource source;
        source.result.isLiveBody = true;
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve(Jemison());

        Require(result.dossierIsLiveBody, "live body was not reported as live");
        Require(!result.resolvedBody, "missing engine system data produced a resolved body");
    }

    void TestValidMoonProducesCompleteResolution()
    {
        FakeBodyResolutionSource source;
        source.result = {
            .isLiveBody = true,
            .body = ::ResolvedBody {
                .id = JemisonId,
                .systemId = AlphaCentauriId,
            },
        };
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve({
            .id = JemisonId,
            .kind = ::ObservedTargetKind::Moon,
            .displayName = "Moon",
        });

        Require(result.dossierId == JemisonId, "complete resolution lost the dossier identity");
        Require(result.dossierIsLiveBody, "complete resolution lost live-form evidence");
        Require(result.resolvedBody.has_value(), "valid body produced no resolution");
        Require(result.resolvedBody->id == JemisonId, "complete resolution retained the wrong body identity");
        Require(result.resolvedBody->systemId == AlphaCentauriId, "complete resolution retained the wrong system identity");
        Require(source.calls == 1 && source.lastBodyId == JemisonId, "complete resolution queried the wrong identity");
    }

    void TestZeroSystemIsRetained()
    {
        FakeBodyResolutionSource source;
        source.result = {
            .isLiveBody = true,
            .body = ::ResolvedBody {
                .id = JemisonId,
                .systemId = 0,
            },
        };
        ::BodyResolver resolver {source};

        const auto result = resolver.Resolve(Jemison());

        Require(result.resolvedBody.has_value(), "valid zero system was treated as missing");
        Require(result.resolvedBody->systemId == 0, "valid zero system changed during resolution");
    }

    void RunTests()
    {
        TestInvalidDossierDoesNotQuerySource();
        TestUnsupportedDossierDoesNotQuerySource();
        TestNonLiveBodyFailsClosed();
        TestLiveBodyCanLackSystemData();
        TestValidMoonProducesCompleteResolution();
        TestZeroSystemIsRetained();
    }
}

void RunBodyResolverTests()
{
    RunTests();
}
