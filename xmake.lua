includes("lib/commonlibsf")

set_project("Starmap Cruise")
set_version("0.5.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target("Starmap Cruise")
    add_rules("commonlibsf.plugin", {
        name = "Starmap Cruise",
        author = "ozooma10",
        description = "Select a planet, moon, or station and set it as your Cruise target directly from the system map."
    })

    add_files("v2/src/**.cpp")
    add_headerfiles("v2/src/**.h")
    add_includedirs("v2/src")

target("Starmap Cruise Native Probe")
    set_default(false)

    add_rules("commonlibsf.plugin", {
        name = "Starmap Cruise Native Probe",
        author = "ozooma10",
        description = "Temporary passive native-contract probe for remote-system Cruise."
    })

    -- The probe is a mutually exclusive diagnostic variant, not a second
    -- plugin to load beside production.  Keep the build artifact uniquely
    -- named; the guarded deployment step alone renames it to shadow the
    -- production VFS filename inside a dedicated MO2 mod.
    set_basename("Starmap Cruise Native Probe")
    set_targetdir("build/$(plat)/$(arch)/$(mode)/remote-native-probe")
    add_defines("CFS_REMOTE_NATIVE_PROBE=1")

    add_files(
        "v2/src/**.cpp",
        "tools/remote-system-native-probe/**.cpp"
    )
    add_headerfiles(
        "v2/src/**.h",
        "tools/remote-system-native-probe/**.h"
    )
    add_includedirs(
        "v2/src",
        "tools/remote-system-native-probe"
    )

target("CruiseFromStarmapV2Tests")
    set_kind("binary")
    set_default(false)

    add_files(
        "v2/tests/**.cpp",
        "v2/src/Navigation/NavigationRuntime.cpp",
        "v2/src/Selection/SelectionPolicy.cpp",
        "v2/src/Presentation/ActionPolicy.cpp",
        "v2/src/Presentation/ActionPresenter.cpp",
        "v2/src/Map/MapSessionState.cpp",
        "v2/src/Application/CruiseRuntime.cpp",
        "v2/src/Starfield/MapObservationInbox.cpp",
        "v2/src/Starfield/HudObservationInbox.cpp",
        "v2/src/Starfield/TravelObservationInbox.cpp",
        "v2/src/Starfield/RemoteRouteProtocol.cpp",
        "v2/src/Starfield/MapActionInputState.cpp"
    )
    add_headerfiles(
        "v2/src/**.h",
        "v2/tests/TestSuites.h"
    )
    add_includedirs(
        "v2/src",
        "v2/tests"
    )
    add_tests("v2")
