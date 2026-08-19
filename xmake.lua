includes("lib/commonlibsf")

set_project("Starmap Cruise")
set_version("0.5.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra", "error")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target("Starmap Cruise")
    add_rules("commonlibsf.plugin", {
        name = "Starmap Cruise",
        author = "ozooma10",
        description = "Select a planet, moon, or station and set it as your Cruise target directly from the system map."
    })

    -- CommonLibSF still uses std::aligned_storage_t; keep project warnings fatal without failing on that C++23 dependency deprecation.
    add_defines("_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING")
    add_sysincludedirs(
        "lib/commonlibsf/include",
        "lib/commonlibsf/lib/commonlib-shared/include"
    )

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")

target("CruiseFromStarmapTests")
    set_kind("binary")
    set_default(false)

    add_files(
        "tests/**.cpp",
        "src/Navigation/NavigationRuntime.cpp",
        "src/Selection/SelectionPolicy.cpp",
        "src/Presentation/ActionPolicy.cpp",
        "src/Presentation/ActionPresenter.cpp",
        "src/Map/MapSessionState.cpp",
        "src/Application/CruiseRuntime.cpp",
        "src/Starfield/MapObservationInbox.cpp",
        "src/Starfield/HudObservationInbox.cpp",
        "src/Starfield/TravelObservationInbox.cpp",
        "src/Starfield/RemoteRouteProtocol.cpp",
        "src/Starfield/MapActionInputState.cpp"
    )
    add_headerfiles(
        "src/**.h",
        "tests/TestSuites.h"
    )
    add_includedirs(
        "src",
        "tests"
    )
    add_tests("core")
