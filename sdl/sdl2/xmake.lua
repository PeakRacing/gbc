set_project("gbc")
set_xmakever("3.0.0")
add_rules("mode.debug", "mode.release")

if is_mode("debug") then
    set_symbols("debug")
    set_optimize("none")
else
    set_strip("all")
    set_symbols("hidden")
    set_optimize("fastest")
end

set_warnings("allextra")
set_languages("c11")
add_cflags("/utf-8", {tools = "cl"})

add_requires("libsdl2", {configs = {sdlmain = false}})
add_packages("libsdl2")

target("gbc", function ()
    set_kind("binary")

    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")

    add_includedirs("port")
    add_files("port/*.c")
    add_files("main.c")
end)

target("apu_test", function ()
    set_kind("binary")

    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/port")
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")
    add_files(gbc_dir .. "/tests/apu_test.c")
end)

