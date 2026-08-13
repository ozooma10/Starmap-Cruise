set_config("commonlib_ini", true)

local commonlibsf_root = os.getenv("COMMONLIBSF_PATH")
if not commonlibsf_root or #commonlibsf_root == 0 then
    commonlibsf_root = path.join(os.scriptdir(), "../OSF RE/lib/commonlibsf")
end
if not os.isdir(commonlibsf_root) then
    raise("CommonLibSF not found; set COMMONLIBSF_PATH to a CommonLibSF checkout")
end
includes(commonlibsf_root)

add_requires("zlib")

set_xmakever("3.0.0")
set_project("CruiseFromStarmap")
set_version("0.1.0")
set_license("GPL-3.0-or-later")
set_arch("x64")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target("CruiseFromStarmap", function()
    add_rules("commonlibsf.plugin", {
        name = "CruiseFromStarmap",
        author = "NICKLEBACK",
        description = "Fail-closed Starmap bridge with vanilla system-level jumps and exact planet, moon, and station Cruise targets."
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h", "src/**.inl")
    add_includedirs("src")
    add_packages("zlib")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")

    add_installfiles("CruiseFromStarmap.ini", { prefixdir = "SFSE/Plugins" })
    add_installfiles("CruiseFromStarmapCustom.ini.example", { prefixdir = "SFSE/Plugins" })

    if not os.getenv("XSE_SF_MODS_PATH") and not os.getenv("XSE_SF_GAME_PATH") then
        set_installdir("$(builddir)/deploy/Data")
    end
end)

target("RecordReaderTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/RecordReaderTests.cpp", "src/BodyIndex/RecordReader.cpp")
    add_includedirs("src")
    add_packages("zlib")
    add_tests("record-reader")
end)

target("CruiseFromStarmapV2Tests", function()
    set_kind("binary")
    set_default(false)

    add_files(
        "v2/tests/**.cpp",
        "v2/src/Navigation/NavigationRuntime.cpp",
        "v2/src/Selection/SelectionPolicy.cpp",
        "v2/src/Presentation/ActionPolicy.cpp",
        "v2/src/Map/MapSessionState.cpp",
        "v2/src/Application/CruiseRuntime.cpp"
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
end)
