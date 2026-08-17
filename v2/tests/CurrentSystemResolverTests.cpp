#include "Application/CurrentSystemResolver.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr ::FormID JemisonId = 0x10;
    constexpr ::FormID MarsId = 0x20;
    constexpr ::FormID LunaId = 0x30;
    constexpr ::FormID AlphaCentauriId = 0x100;
    constexpr ::FormID SolId = 0;

    class FakeBodyResolutionSource final : public ::BodyResolutionSource
    {
    public:
        std::optional<::ResolvedBody> ResolveBody(::FormID bodyId) const override
        {
            const auto found = m_bodies.find(bodyId);
            if (found == m_bodies.end())
                return std::nullopt;

            return found->second;
        }

        std::unordered_map<::FormID, ::ResolvedBody> m_bodies;
    };

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    void TestEmptyObservationIsUnresolved()
    {
        FakeBodyResolutionSource bodies;
        const std::vector<::FormID> bodyIds;

        Require(!::ResolveCurrentSystem(bodyIds, bodies), "empty observation resolved a current system");
    }

    void TestUnknownBodiesAreIgnored()
    {
        FakeBodyResolutionSource bodies;
        const std::vector<::FormID> bodyIds {JemisonId, MarsId};

        Require(!::ResolveCurrentSystem(bodyIds, bodies), "unknown bodies resolved a current system");
    }

    void TestUniqueMajorityResolvesSystem()
    {
        FakeBodyResolutionSource bodies;
        bodies.m_bodies = {
            {JemisonId, {.id = JemisonId, .systemId = AlphaCentauriId}},
            {MarsId, {.id = MarsId, .systemId = SolId}},
            {LunaId, {.id = LunaId, .systemId = SolId}},
        };

        const std::vector<::FormID> bodyIds {JemisonId, MarsId, LunaId};
        const auto systemId = ::ResolveCurrentSystem(bodyIds, bodies);

        Require(systemId == SolId, "unique system majority was not resolved");
    }

    void TestTiedSystemsRemainUnresolved()
    {
        FakeBodyResolutionSource bodies;
        bodies.m_bodies = {
            {JemisonId, {.id = JemisonId, .systemId = AlphaCentauriId}},
            {MarsId, {.id = MarsId, .systemId = SolId}},
        };

        const std::vector<::FormID> bodyIds {JemisonId, MarsId};

        Require(!::ResolveCurrentSystem(bodyIds, bodies), "tied systems produced an arbitrary result");
    }

    void TestMismatchedBodyIsIgnored()
    {
        FakeBodyResolutionSource bodies;
        bodies.m_bodies.emplace(JemisonId, ::ResolvedBody {.id = MarsId, .systemId = AlphaCentauriId});
        const std::vector<::FormID> bodyIds {JemisonId};

        Require(!::ResolveCurrentSystem(bodyIds, bodies), "mismatched body resolution was accepted");
    }

    void TestZeroBodyIdIsNotARealObservation()
    {
        FakeBodyResolutionSource bodies;
        bodies.m_bodies.emplace(0, ::ResolvedBody {.id = 0, .systemId = AlphaCentauriId});
        const std::vector<::FormID> bodyIds {0};

        Require(!::ResolveCurrentSystem(bodyIds, bodies), "zero body ID was treated as a real observation");
    }

    void RunTests()
    {
        TestEmptyObservationIsUnresolved();
        TestUnknownBodiesAreIgnored();
        TestUniqueMajorityResolvesSystem();
        TestTiedSystemsRemainUnresolved();
        TestMismatchedBodyIsIgnored();
        TestZeroBodyIdIsNotARealObservation();
    }
}

void RunCurrentSystemResolverTests()
{
    RunTests();
}
