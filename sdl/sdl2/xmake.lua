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

target("gbc", function ()
    set_kind("binary")
    add_packages("libsdl2")

    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")

    add_includedirs("port")
    add_files("port/*.c")
    add_files("main.c")
end)

target("sdl_storage_test", function ()
    set_kind("binary")
    add_packages("libsdl2")
    add_includedirs("../../inc", "port")
    add_files("../../src/**.c", "port/*.c", "../../tests/sdl_storage_test.c")
end)

target("core_test", function ()
    set_kind("binary")

    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/port")
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")
    add_files(gbc_dir .. "/tests/regression_test.c")
    add_files(gbc_dir .. "/tests/fs_hooks.c")
end)

target("rom_test", function ()
    set_kind("binary")

    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/rom_port")
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")
    add_files(gbc_dir .. "/tests/rom_smoke_test.c")
end)

target("rom_test_stream", function ()
    set_kind("binary")

    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/rom_stream_port")
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")
    add_files(gbc_dir .. "/tests/rom_smoke_test.c")
end)

target("sound_test", function ()
    set_kind("binary")

    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/sound_port")
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")
    add_files(gbc_dir .. "/tests/apu_regression_test.c")
    add_files(gbc_dir .. "/tests/fs_hooks.c")
end)


target("low_memory_test", function ()
    set_kind("binary")
    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/low_memory_port")
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")
    add_files(gbc_dir .. "/tests/low_memory_test.c")
end)

target("feature_test", function ()
    set_kind("binary")
    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/feature_port")
    add_includedirs(gbc_dir .. "/inc")
    add_files(gbc_dir .. "/src/**.c")
    add_files(gbc_dir .. "/tests/feature_test.c")
end)

target("feature_sound_test", function ()
    set_kind("binary")
    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/feature_port", gbc_dir .. "/inc")
    add_defines("GBC_ENABLE_SOUND=1")
    add_files(gbc_dir .. "/src/**.c", gbc_dir .. "/tests/feature_test.c")
end)

for _, streamed in ipairs({false, true}) do
    target(streamed and "rom_state_stream_test" or "rom_state_test", function ()
        set_kind("binary")
        local gbc_dir = "../.."
        add_includedirs(gbc_dir .. (streamed and "/tests/rom_stream_port" or "/tests/rom_port"), gbc_dir .. "/inc")
        add_defines("GBC_ENABLE_SAVE_STATE=1", "GBC_SMOKE_STATE_REPLAY=1")
        add_files(gbc_dir .. "/src/**.c", gbc_dir .. "/tests/rom_smoke_test.c", gbc_dir .. "/tests/fs_hooks.c")
    end)
end

target("boot_test", function ()
    set_kind("binary")
    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/feature_port", gbc_dir .. "/inc")
    add_defines("GBC_ENABLE_BOOT_ROM=1", "GBC_ENABLE_SOUND=1")
    add_files(gbc_dir .. "/src/**.c", gbc_dir .. "/tests/feature_test.c")
end)

target("low_memory_boot_test", function ()
    set_kind("binary")
    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/low_memory_port", gbc_dir .. "/inc")
    add_defines("GBC_ENABLE_BOOT_ROM=1", "GBC_ENABLE_SAVE_STATE=1", "GBC_ENABLE_RTC_SAVE=1")
    add_files(gbc_dir .. "/src/**.c", gbc_dir .. "/tests/low_memory_test.c")
end)

target("feature_stream_test", function ()
    set_kind("binary")
    local gbc_dir = "../.."
    add_includedirs(gbc_dir .. "/tests/feature_port", gbc_dir .. "/inc")
    add_defines("GBC_ROM_STREAM=1")
    add_files(gbc_dir .. "/src/**.c", gbc_dir .. "/tests/feature_test.c")
end)

for _, rtc_only in ipairs({false, true}) do
    target(rtc_only and "rtc_only_test" or "battery_only_test", function ()
        set_kind("binary")
        local gbc_dir = "../.."
        add_includedirs(gbc_dir .. "/tests/feature_port", gbc_dir .. "/inc")
        add_defines("GBC_ENABLE_SAVE_STATE=0")
        if rtc_only then
            add_defines("GBC_ENABLE_SRAM_SAVE=0", "GBC_ENABLE_RTC_SAVE=1")
        else
            add_defines("GBC_ENABLE_RTC_SAVE=0")
        end
        add_files(gbc_dir .. "/src/**.c", gbc_dir .. "/tests/feature_test.c")
    end)
end
