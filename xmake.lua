-- include subprojects
includes("lib/commonlibsse-ng")

-- set project constants
set_project("ItemInspection")
set_version("2.0.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")

-- define targets
target("ItemInspection")
    add_rules("commonlibsse-ng.plugin", {
        name = "ItemInspection",
        author = "Iuko",
        description = "Picked up items go to your hand first: look at them, turn them, then put them in your backpack"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- SKSE Menu Framework API (header only, from the framework's author)
    add_headerfiles("lib/SKSEMenuFramework/*.h")
    add_includedirs("lib/SKSEMenuFramework")

    -- SmoothCam modder API (header only, from SmoothCam's author)
    add_headerfiles("lib/SmoothCamAPI/*.h")
    add_includedirs("lib/SmoothCamAPI")
