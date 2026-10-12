/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
#define main core_reference_suite
#define gbc_sound_output discarded_sound_output
#define gbc_frame discarded_frame
#include "regression_test.c"
#undef main
#undef gbc_sound_output
#undef gbc_frame
#include "../src/gbc_fast.h"

static uint32_t pcm_frames, pcm_nonzero;
static int callback_switch;
int gbc_sound_output(uint8_t *buffer, size_t bytes)
{
    pcm_frames += (uint32_t)(bytes / 4);
    for (size_t i = 0; i < bytes; i++) pcm_nonzero += buffer[i] != 0;
    return GBC_OK;
}
void gbc_frame(gbc_t *gbc) { callback_switch = gbc_set_run_mode(gbc, GBC_RUN_REFERENCE); }

static void fast_setup(gbc_t *gbc, uint8_t cgb)
{
    static uint8_t rom[32768];
    fill_test_rom(rom, 0, 0);
    rom[0x143] = cgb ? 0x80 : 0;
    rom[0x100] = 0x18; rom[0x101] = 0xFE; /* JR -2 */
    expect_int("init", gbc_init_static(gbc), GBC_OK);
    expect_int("load", gbc_load_rom(gbc, rom, sizeof(rom)), GBC_OK);
    expect_int("fast", gbc_set_run_mode(gbc, GBC_RUN_FAST), GBC_OK);
}

static void test_budget_and_sync(void)
{
    gbc_t gbc;
    fast_setup(&gbc, 1);
    expect_int("zero budget", gbc_run_cycles(&gbc, 0), 0);
    expect_int("instruction overshoot", gbc_run_cycles(&gbc, 1), 12);
    expect_int("cross budget", gbc_run_cycles(&gbc, 13), 24);
    expect_u16("CPU exact", gbc.cpu.pc, 0x100);
    expect_u16("timer follows CPU", gbc.timer.div_counter, 36);
    expect_u16("PPU follows CPU", gbc.ppu.dot_cycles, 36);
    gbc.mmu.double_speed = 1;
    expect_int("double speed", gbc_run_cycles(&gbc, 1), 12);
    expect_u16("double PPU", gbc.ppu.dot_cycles, 42);
    expect_u16("double timer", gbc.timer.div_counter, 48);
    gbc.mmu.io[0x40] = 0; gbc.cpu.halted = 1;
    gbc.mmu.ie = 0; gbc.mmu.io[0x0F] = 0;
    gbc.mmu.serial_cycles = 16;
    gbc_run_cycles(&gbc, 16);
    expect_u8("HALT remains", gbc.cpu.halted, 1);
    expect_u8("serial IRQ", gbc.mmu.io[0x0F] & GBC_IF_SERIAL, GBC_IF_SERIAL);
    expect_u8("LCD off LY", gbc.mmu.io[0x44], 0);
    gbc.gbc_quit = 1;
    expect_int("quit budget", gbc_run_cycles(&gbc, 512), 0);
    gbc_deinit_static(&gbc);
}

static void test_line_latch_and_cache(void)
{
    gbc_t gbc;
    fast_setup(&gbc, 0);
    gbc.mmu.io[0x40] = 0x91; gbc.mmu.io[0x47] = 0xE4;
    for (unsigned y = 0; y < 8; y++) gbc_mmu_write(&gbc, (uint16_t)(0x8000 + y * 2), 0xFF);
    gbc_ppu_fast_step(&gbc, 80);
    expect_u8("mode 3", gbc.ppu.mode, 3);
    gbc_mmu_write(&gbc, 0xFF47, 0xFC);
    gbc_ppu_fast_step(&gbc, 172);
    expect_u8("HBlank", gbc.ppu.mode, 0);
    expect_int("latched color", gbc.gbc_draw_data[0], gbc_ppu_color(21, 21, 21));
    expect_int("decoded row clean", gbc.fast->dirty[0][0] & 1, 0);
    gbc_mmu_write(&gbc, 0x8000, 0);
    expect_int("write dirty", gbc.fast->dirty[0][0] & 1, 1);
    gbc_ppu_fast_step(&gbc, 204);
    expect_u8("next LY", gbc.mmu.io[0x44], 1);
    gbc_ppu_fast_step(&gbc, 80); gbc_ppu_fast_step(&gbc, 172);
    expect_int("next palette", gbc.gbc_draw_data[160], gbc_ppu_color(0, 0, 0));
    gbc_deinit_static(&gbc);
}

static void test_audio_cadence_and_lifecycle(void)
{
    gbc_t gbc;
    fast_setup(&gbc, 0);
    gbc_apu_write(&gbc, 0x26, 0);
    pcm_frames = pcm_nonzero = 0;
    uint32_t clocks = gbc_run_cycles(&gbc, 4194304);
    uint32_t samples = pcm_frames + gbc.apu.sample_index;
    expect_int("sample cadence", samples, (uint32_t)(((uint64_t)clocks * GBC_APU_SAMPLE_RATE) / GBC_CPU_CLOCK_FREQ));
    expect_int("silent PCM", pcm_nonzero, 0);
    expect_int("callback rejects switch", callback_switch, GBC_ERROR_UNSUPPORTED);
    expect_int("save fast", gbc_state_save_file(&gbc, "fast-test.state"), GBC_OK);
    gbc_mmu_write(&gbc, 0x8000, 0xA5);
    expect_int("load fast", gbc_state_load_file(&gbc, "fast-test.state"), GBC_OK);
    expect_int("load retains mode", gbc.run_mode, GBC_RUN_FAST);
    expect_int("load invalidates tiles", gbc.fast->dirty[0][0], 255);
    remove("fast-test.state"); remove("fast-test.state.bak");
    expect_int("reference mode", gbc_set_run_mode(&gbc, GBC_RUN_REFERENCE), GBC_OK);
    expect_int("reference frees workspace", gbc.fast == NULL, 1);
    test_fail_allocation = 1;
    expect_int("workspace failure", gbc_set_run_mode(&gbc, GBC_RUN_FAST), GBC_ERROR_ALLOC);
    expect_int("failure retains mode", gbc.run_mode, GBC_RUN_REFERENCE);
    test_fail_allocation = 0;
    gbc_deinit_static(&gbc);
}

int main(void)
{
    core_reference_suite();
    test_budget_and_sync(); test_line_latch_and_cache(); test_audio_cadence_and_lifecycle();
    printf("fast workspace=%u bytes; fast tests %s\n", (unsigned)sizeof(gbc_fast_t), test_failures ? "FAILED" : "passed");
    return test_failures ? 1 : 0;
}
