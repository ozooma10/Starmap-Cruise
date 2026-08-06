set_config("commonlib_ini", true)

-- Use the clean, runtime-proven shared mirror that OSF RE builds against. The
-- workspace-root CommonLibSF checkout may carry unrelated active branch work;
-- this mod does not vendor or modify either dependency tree.
includes("../OSF RE/lib/commonlibsf")

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
        description = "Fail-closed Starmap bridge with vanilla-route handoff and jump-persistent Cruise targeting."
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    add_packages("zlib")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")

    add_installfiles("CruiseFromStarmap.ini", { prefixdir = "SFSE/Plugins" })
    add_installfiles("CruiseFromStarmapCustom.ini.example", { prefixdir = "SFSE/Plugins" })

    if not os.getenv("XSE_SF_MODS_PATH") and not os.getenv("XSE_SF_GAME_PATH") then
        set_installdir("$(builddir)/deploy/Data")
    end

    after_build(function(target)
        local modsroot = os.getenv("XSE_SF_MODS_PATH")
        if not modsroot or #modsroot == 0 then
            return
        end
        local plugindir = path.join(modsroot, target:name(), "SFSE", "Plugins")
        os.mkdir(plugindir)
        os.trycp(target:targetfile(), path.join(plugindir, path.filename(target:targetfile())))
        if target:symbolfile() and os.isfile(target:symbolfile()) then
            os.trycp(target:symbolfile(), path.join(plugindir, path.filename(target:symbolfile())))
        end
        os.trycp("CruiseFromStarmap.ini", path.join(plugindir, "CruiseFromStarmap.ini"))
        if not os.isfile(path.join(plugindir, "CruiseFromStarmapCustom.ini.example")) then
            os.trycp("CruiseFromStarmapCustom.ini.example",
                path.join(plugindir, "CruiseFromStarmapCustom.ini.example"))
        end
        cprint("${green}[CruiseFromStarmap] deployed DLL/PDB/default INI to %s", plugindir)
    end)
end)
