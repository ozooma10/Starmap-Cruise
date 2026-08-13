#include "Bodies/PluginFormId.h"
#include "TestSuites.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error{ std::string{ message } };
    }

    void RequireResolved(
        const ::PluginFormIdResolver& resolver,
        ::FormID pluginFormId,
        ::FormID expected,
        std::string_view message)
    {
        const auto resolved = resolver.Resolve(pluginFormId);

        Require(resolved.has_value(), message);
        Require(*resolved == expected, message);
    }

    void TestFullPluginSelfRecord()
    {
        const ::PluginFormIdResolver resolver{
            {},
            {
                .tier = ::PluginTier::Full,
                .index = 5,
            },
        };

        Require(resolver.IsValid(),
            "valid full-plugin resolver was rejected");
        RequireResolved(
            resolver,
            0x00001234,
            0x05001234,
            "full-plugin self record was encoded incorrectly");
    }

    void TestMediumPluginSelfRecord()
    {
        const ::PluginFormIdResolver resolver{
            {},
            {
                .tier = ::PluginTier::Medium,
                .index = 3,
            },
        };

        RequireResolved(
            resolver,
            0x00001234,
            0xFD031234,
            "medium-plugin self record was encoded incorrectly");
    }

    void TestSmallPluginSelfRecord()
    {
        const ::PluginFormIdResolver resolver{
            {},
            {
                .tier = ::PluginTier::Small,
                .index = 2,
            },
        };

        RequireResolved(
            resolver,
            0x00000234,
            0xFE002234,
            "small-plugin self record was encoded incorrectly");
    }

    void TestOwnerSlotsResolveMastersAndSelf()
    {
        const ::PluginFormIdResolver resolver{
            {
                {
                    .tier = ::PluginTier::Full,
                    .index = 0,
                },
                {
                    .tier = ::PluginTier::Medium,
                    .index = 3,
                },
                {
                    .tier = ::PluginTier::Small,
                    .index = 2,
                },
            },
            {
                .tier = ::PluginTier::Full,
                .index = 5,
            },
        };

        RequireResolved(
            resolver,
            0x00000410,
            0x00000410,
            "full-master override resolved incorrectly");
        RequireResolved(
            resolver,
            0x01000600,
            0xFD030600,
            "medium-master override resolved incorrectly");
        RequireResolved(
            resolver,
            0x02000678,
            0xFE002678,
            "small-master override resolved incorrectly");
        RequireResolved(
            resolver,
            0x03000500,
            0x05000500,
            "self-owned record after masters resolved incorrectly");
    }

    void TestNonzeroFullMasterIndexIsEncoded()
    {
        const ::PluginFormIdResolver resolver{
            {
                {
                    .tier = ::PluginTier::Full,
                    .index = 2,
                },
            },
            {
                .tier = ::PluginTier::Full,
                .index = 5,
            },
        };

        RequireResolved(
            resolver,
            0x00000410,
            0x02000410,
            "nonzero full-master load index was ignored");
        RequireResolved(
            resolver,
            0x01000500,
            0x05000500,
            "single-master self slot resolved incorrectly");
    }

    void TestMaximumTierIndicesAreValid()
    {
        const ::PluginLoadIdentity full{
            .tier = ::PluginTier::Full,
            .index = 0xFC,
        };
        const ::PluginLoadIdentity medium{
            .tier = ::PluginTier::Medium,
            .index = 0xFF,
        };
        const ::PluginLoadIdentity small{
            .tier = ::PluginTier::Small,
            .index = 0xFFF,
        };

        Require(full.IsValid(),
            "maximum full-plugin index was rejected");
        Require(medium.IsValid(),
            "maximum medium-plugin index was rejected");
        Require(small.IsValid(),
            "maximum small-plugin index was rejected");

        RequireResolved(
            ::PluginFormIdResolver{ {}, full },
            0x00FFFFFF,
            0xFCFFFFFF,
            "maximum full-plugin identity was encoded incorrectly");
        RequireResolved(
            ::PluginFormIdResolver{ {}, medium },
            0x0000FFFF,
            0xFDFFFFFF,
            "maximum medium-plugin identity was encoded incorrectly");
        RequireResolved(
            ::PluginFormIdResolver{ {}, small },
            0x00000FFF,
            0xFEFFFFFF,
            "maximum small-plugin identity was encoded incorrectly");
    }

    void TestInvalidTierIndicesFailResolver()
    {
        const ::PluginLoadIdentity invalidFull{
            .tier = ::PluginTier::Full,
            .index = 0xFD,
        };
        const ::PluginLoadIdentity invalidMedium{
            .tier = ::PluginTier::Medium,
            .index = 0x100,
        };
        const ::PluginLoadIdentity invalidSmall{
            .tier = ::PluginTier::Small,
            .index = 0x1000,
        };

        Require(!invalidFull.IsValid(),
            "reserved full-plugin index was accepted");
        Require(!invalidMedium.IsValid(),
            "oversized medium-plugin index was accepted");
        Require(!invalidSmall.IsValid(),
            "oversized small-plugin index was accepted");

        const ::PluginFormIdResolver invalidSelf{
            {},
            invalidFull,
        };
        Require(!invalidSelf.IsValid(),
            "resolver accepted an invalid self identity");
        Require(!invalidSelf.Resolve(0x00000100),
            "invalid self identity resolved a nonzero FormID");

        const ::PluginFormIdResolver invalidMaster{
            { invalidSmall },
            {
                .tier = ::PluginTier::Full,
                .index = 1,
            },
        };
        Require(!invalidMaster.IsValid(),
            "resolver accepted an invalid master identity");
        Require(!invalidMaster.Resolve(0x00000100),
            "invalid master identity resolved a nonzero FormID");
    }

    void TestTierLocalOverflowFailsClosed()
    {
        const ::PluginFormIdResolver medium{
            {},
            {
                .tier = ::PluginTier::Medium,
                .index = 3,
            },
        };
        const ::PluginFormIdResolver small{
            {},
            {
                .tier = ::PluginTier::Small,
                .index = 2,
            },
        };

        Require(!medium.Resolve(0x00010000),
            "oversized medium local ID was truncated and accepted");
        Require(!small.Resolve(0x00001000),
            "oversized small local ID was truncated and accepted");
    }

    void TestUnknownOwnerSlotFailsClosed()
    {
        const ::PluginFormIdResolver resolver{
            {
                {
                    .tier = ::PluginTier::Full,
                    .index = 0,
                },
            },
            {
                .tier = ::PluginTier::Full,
                .index = 5,
            },
        };

        Require(!resolver.Resolve(0x02000100),
            "unknown owner slot was reinterpreted as self");
    }

    void TestNullFormIdRemainsNull()
    {
        const ::PluginFormIdResolver resolver{
            {},
            {
                .tier = ::PluginTier::Small,
                .index = 0x1000,
            },
        };

        const auto resolved = resolver.Resolve(0);

        Require(resolved.has_value(),
            "null FormID was rejected");
        Require(*resolved == 0,
            "null FormID was assigned a plugin prefix");
    }

    void TestTooManyMastersInvalidatesResolver()
    {
        const std::vector<::PluginLoadIdentity> masters(
            0x100,
            {
                .tier = ::PluginTier::Full,
                .index = 0,
            });

        const ::PluginFormIdResolver resolver{
            masters,
            {
                .tier = ::PluginTier::Full,
                .index = 1,
            },
        };

        Require(!resolver.IsValid(),
            "resolver accepted an unrepresentable self owner slot");
        Require(!resolver.Resolve(0x00000100),
            "resolver with too many masters resolved a FormID");
    }

    void RunTests()
    {
        TestFullPluginSelfRecord();
        TestMediumPluginSelfRecord();
        TestSmallPluginSelfRecord();
        TestOwnerSlotsResolveMastersAndSelf();
        TestNonzeroFullMasterIndexIsEncoded();
        TestMaximumTierIndicesAreValid();
        TestInvalidTierIndicesFailResolver();
        TestTierLocalOverflowFailsClosed();
        TestUnknownOwnerSlotFailsClosed();
        TestNullFormIdRemainsNull();
        TestTooManyMastersInvalidatesResolver();
    }
}

void RunPluginFormIdTests()
{
    RunTests();
}
