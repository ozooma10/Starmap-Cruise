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
