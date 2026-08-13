#include "Bodies/PluginResolverBuilder.h"
#include "TestSuites.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error {std::string {message}};
    }

    void RequireResolved(const ::PluginFormIdResolver& resolver, ::FormID pluginFormId, ::FormID expected, std::string_view message)
    {
        const auto resolved = resolver.Resolve(pluginFormId);

        Require(resolved.has_value(), message);
        Require(*resolved == expected, message);
    }

    std::vector<::ActivePluginIdentity> MixedTierPlugins()
    {
        return {
            {
                .name = "Starfield.esm",
                .identity =
                    {
                        .tier = ::PluginTier::Full,
                        .index = 0,
                    },
            },
            {
                .name = "Medium.esm",
                .identity =
                    {
                        .tier = ::PluginTier::Medium,
                        .index = 3,
                    },
            },
            {
                .name = "Small.esl",
                .identity =
                    {
                        .tier = ::PluginTier::Small,
                        .index = 2,
                    },
            },
            {
                .name = "Patch.esp",
                .identity = {
                    .tier = ::PluginTier::Full,
                    .index = 5,
                },
            },
        };
    }

    void RequireFailure(const ::PluginResolverBuildOutput& output, ::PluginResolverBuildStatus expectedStatus, std::string_view message)
    {
        Require(output.status == expectedStatus, message);
        Require(!output.Succeeded(), "failed resolver build reported success");
        Require(!output.resolver.has_value(), "failed resolver build exposed a resolver");
    }

    void RequireActiveFailure(const ::PluginResolverBuildOutput& output, ::PluginResolverBuildStatus expectedStatus, std::size_t expectedIndex, std::string_view message)
    {
        RequireFailure(output, expectedStatus, message);
        Require(output.activePluginIndex.has_value() && *output.activePluginIndex == expectedIndex, "active-plugin failure reported the wrong input index");
        Require(!output.masterIndex.has_value(), "active-plugin failure reported a master index");
    }

    void RequireMasterFailure(const ::PluginResolverBuildOutput& output, ::PluginResolverBuildStatus expectedStatus, std::size_t expectedIndex, std::string_view message)
    {
        RequireFailure(output, expectedStatus, message);
        Require(output.masterIndex.has_value() && *output.masterIndex == expectedIndex, "master failure reported the wrong input index");
        Require(!output.activePluginIndex.has_value(), "master failure reported an active-plugin index");
    }

    void RequireGlobalFailure(const ::PluginResolverBuildOutput& output, ::PluginResolverBuildStatus expectedStatus, std::string_view message)
    {
        RequireFailure(output, expectedStatus, message);
        Require(!output.activePluginIndex.has_value(), "global resolver failure reported an active-plugin index");
        Require(!output.masterIndex.has_value(), "global resolver failure reported a master index");
    }

    void TestSelfOnlyResolverUsesCaseInsensitiveName()
    {
        const std::vector<::ActivePluginIdentity> plugins {
            {
                .name = "Self.ESL",
                .identity = {
                    .tier = ::PluginTier::Small,
                    .index = 2,
                },
            },
        };

        const std::vector<std::string> masters;
        const auto output = ::BuildPluginFormIdResolver(plugins, "self.esl", masters);

        Require(output.Succeeded(), "valid self-only resolver build failed");
        Require(output.status == ::PluginResolverBuildStatus::Success, "valid self-only resolver returned the wrong status");
        Require(output.resolver.has_value(), "valid self-only build returned no resolver");
        Require(!output.activePluginIndex.has_value() && !output.masterIndex.has_value(), "successful resolver build reported a problem index");
        RequireResolved(*output.resolver, 0x00000234, 0xFE002234, "self-only compact resolver encoded the wrong FormID");
    }

    void TestMixedTierMastersPreserveOwnerSlotOrder()
    {
        const auto plugins = MixedTierPlugins();
        const std::vector<std::string> masters {
            "STARFIELD.ESM",
            "medium.ESM",
            "SMALL.ESL",
        };

        const auto output = ::BuildPluginFormIdResolver(plugins, "PATCH.ESP", masters);

        Require(output.Succeeded(), "valid mixed-tier resolver build failed");
        RequireResolved(*output.resolver, 0x00000410, 0x00000410, "owner slot zero did not resolve through the first full master");
        RequireResolved(*output.resolver, 0x01000600, 0xFD030600, "owner slot one did not resolve through the medium master");
        RequireResolved(*output.resolver, 0x02000678, 0xFE002678, "owner slot two did not resolve through the small master");
        RequireResolved(*output.resolver, 0x03000500, 0x05000500, "self owner slot did not resolve through the patch identity");
    }

    void TestMaximumMasterCountSucceeds()
    {
        constexpr std::size_t masterCount = 0xFF;

        std::vector<::ActivePluginIdentity> plugins;
        std::vector<std::string> masters;
        plugins.reserve(masterCount + 1);
        masters.reserve(masterCount);

        for (std::size_t index = 0; index < masterCount; ++index) {
            auto name = "Master" + std::to_string(index) + ".esm";
            masters.push_back(name);
            plugins.push_back({
                .name = std::move(name),
                .identity = {
                    .tier = ::PluginTier::Medium,
                    .index = static_cast<std::uint16_t>(index),
                },
            });
        }

        plugins.push_back({
            .name = "Self.esm",
            .identity = {
                .tier = ::PluginTier::Medium,
                .index = 0xFF,
            },
        });

        const auto output = ::BuildPluginFormIdResolver(plugins, "Self.esm", masters);

        Require(output.Succeeded(), "maximum representable master list was rejected");
        RequireResolved(*output.resolver, 0xFE000001, 0xFDFE0001, "last master owner slot resolved incorrectly");
        RequireResolved(*output.resolver, 0xFF000001, 0xFDFF0001, "maximum self owner slot resolved incorrectly");
    }

    void TestInvalidActivePluginNamesFail()
    {
        const std::vector<std::string> invalidNames {
            "",
            "Bad.txt",
            "Folder/Bad.esm",
            "Folder\\Bad.esm",
            std::string {"Bad\0.esm", 8},
        };

        for (const auto& invalidName : invalidNames) {
            std::vector<::ActivePluginIdentity> plugins {
                {
                    .name = "Self.esm",
                    .identity =
                        {
                            .tier = ::PluginTier::Full,
                            .index = 5,
                        },
                },
                {
                    .name = invalidName,
                    .identity = {
                        .tier = ::PluginTier::Full,
                        .index = 6,
                    },
                },
            };

            const std::vector<std::string> masters;
            const auto output = ::BuildPluginFormIdResolver(plugins, "Self.esm", masters);

            RequireActiveFailure(output, ::PluginResolverBuildStatus::InvalidActivePluginName, 1, "invalid active plugin name returned the wrong status");
        }
    }

    void TestInvalidActivePluginIdentityFails()
    {
        std::vector<::ActivePluginIdentity> plugins {
            {
                .name = "Self.esm",
                .identity =
                    {
                        .tier = ::PluginTier::Full,
                        .index = 5,
                    },
            },
            {
                .name = "Invalid.esm",
                .identity = {
                    .tier = ::PluginTier::Full,
                    .index = 0xFD,
                },
            },
        };

        const std::vector<std::string> masters;
        const auto output = ::BuildPluginFormIdResolver(plugins, "Self.esm", masters);

        RequireActiveFailure(output, ::PluginResolverBuildStatus::InvalidActivePluginIdentity, 1, "invalid active plugin identity returned the wrong status");
    }

    void TestDuplicateActivePluginNameFailsCaseInsensitively()
    {
        std::vector<::ActivePluginIdentity> plugins {
            {
                .name = "Self.esm",
                .identity =
                    {
                        .tier = ::PluginTier::Full,
                        .index = 5,
                    },
            },
            {
                .name = "SELF.ESM",
                .identity = {
                    .tier = ::PluginTier::Medium,
                    .index = 3,
                },
            },
        };

        const std::vector<std::string> masters;
        const auto output = ::BuildPluginFormIdResolver(plugins, "Self.esm", masters);

        RequireActiveFailure(output, ::PluginResolverBuildStatus::DuplicateActivePluginName, 1, "case-equivalent active plugin names were accepted");
    }

    void TestDuplicateActivePluginIdentityFails()
    {
        std::vector<::ActivePluginIdentity> plugins {
            {
                .name = "Self.esm",
                .identity =
                    {
                        .tier = ::PluginTier::Full,
                        .index = 5,
                    },
            },
            {
                .name = "Other.esm",
                .identity = {
                    .tier = ::PluginTier::Full,
                    .index = 5,
                },
            },
        };

        const std::vector<std::string> masters;
        const auto output = ::BuildPluginFormIdResolver(plugins, "Self.esm", masters);

        RequireActiveFailure(output, ::PluginResolverBuildStatus::DuplicateActivePluginIdentity, 1, "duplicate active load identity was accepted");
    }

    void TestInvalidSelfPluginNameFails()
    {
        const auto plugins = MixedTierPlugins();
        const std::vector<std::string> masters;
        const auto output = ::BuildPluginFormIdResolver(plugins, "Folder/Patch.esp", masters);

        RequireGlobalFailure(output, ::PluginResolverBuildStatus::InvalidSelfPluginName, "invalid self plugin name returned the wrong status");
    }

    void TestInactiveSelfPluginFails()
    {
        const auto plugins = MixedTierPlugins();
        const std::vector<std::string> masters;
        const auto output = ::BuildPluginFormIdResolver(plugins, "Missing.esp", masters);

        RequireGlobalFailure(output, ::PluginResolverBuildStatus::SelfPluginNotActive, "inactive self plugin returned the wrong status");
    }

    void TestTooManyMastersFailsBeforeMasterIteration()
    {
        const auto plugins = MixedTierPlugins();
        const std::vector<std::string> masters(0x100, "Starfield.esm");
        const auto output = ::BuildPluginFormIdResolver(plugins, "Patch.esp", masters);

        RequireGlobalFailure(output, ::PluginResolverBuildStatus::TooManyMasters, "oversized master list returned the wrong status");
    }

    void TestInvalidMasterNamesFail()
    {
        const auto plugins = MixedTierPlugins();
        const std::vector<std::string> invalidNames {
            "",
            "Bad.txt",
            "Folder/Bad.esm",
            "Folder\\Bad.esm",
            std::string {"Bad\0.esm", 8},
        };

        for (const auto& invalidName : invalidNames) {
            const std::vector<std::string> masters {
                "Starfield.esm",
                invalidName,
            };

            const auto output = ::BuildPluginFormIdResolver(plugins, "Patch.esp", masters);

            RequireMasterFailure(output, ::PluginResolverBuildStatus::InvalidMasterName, 1, "invalid master name returned the wrong status");
        }
    }

    void TestDuplicateMasterNameFailsCaseInsensitively()
    {
        const auto plugins = MixedTierPlugins();
        const std::vector<std::string> masters {
            "Starfield.esm",
            "STARFIELD.ESM",
        };

        const auto output = ::BuildPluginFormIdResolver(plugins, "Patch.esp", masters);

        RequireMasterFailure(output, ::PluginResolverBuildStatus::DuplicateMasterName, 1, "case-equivalent duplicate masters were accepted");
    }

    void TestSelfListedAsMasterFailsCaseInsensitively()
    {
        const auto plugins = MixedTierPlugins();
        const std::vector<std::string> masters {
            "Starfield.esm",
            "PATCH.ESP",
        };

        const auto output = ::BuildPluginFormIdResolver(plugins, "Patch.esp", masters);

        RequireMasterFailure(output, ::PluginResolverBuildStatus::SelfListedAsMaster, 1, "self plugin was accepted as its own master");
    }

    void TestInactiveMasterFails()
    {
        const auto plugins = MixedTierPlugins();
        const std::vector<std::string> masters {
            "Starfield.esm",
            "Missing.esm",
        };

        const auto output = ::BuildPluginFormIdResolver(plugins, "Patch.esp", masters);

        RequireMasterFailure(output, ::PluginResolverBuildStatus::MasterPluginNotActive, 1, "inactive master returned the wrong status");
    }

    void RunTests()
    {
        TestSelfOnlyResolverUsesCaseInsensitiveName();
        TestMixedTierMastersPreserveOwnerSlotOrder();
        TestMaximumMasterCountSucceeds();
        TestInvalidActivePluginNamesFail();
        TestInvalidActivePluginIdentityFails();
        TestDuplicateActivePluginNameFailsCaseInsensitively();
        TestDuplicateActivePluginIdentityFails();
        TestInvalidSelfPluginNameFails();
        TestInactiveSelfPluginFails();
        TestTooManyMastersFailsBeforeMasterIteration();
        TestInvalidMasterNamesFail();
        TestDuplicateMasterNameFailsCaseInsensitively();
        TestSelfListedAsMasterFailsCaseInsensitively();
        TestInactiveMasterFails();
    }
}

void RunPluginResolverBuilderTests()
{
    RunTests();
}
