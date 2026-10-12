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
    gbc_fast_dmg_palette_write(&gbc, 0x47);
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

static void test_instruction_and_idle_equivalence(void)
{
    gbc_t fast, ref;
    for (unsigned scenario = 0; scenario < 256; scenario++) {
        fast_setup(&fast, 1);
        fast.cpu.halted = scenario & 1;
        fast.cpu.ime = (scenario >> 1) & 1;
        fast.mmu.double_speed = (scenario >> 2) & 1;
        fast.mmu.io[0x07] = (uint8_t)((scenario >> 3) & 7);
        fast.mmu.io[0x05] = (scenario & 0x40) ? 255 : 13;
        fast.mmu.io[0x06] = 57;
        fast.mmu.ie = (scenario & 0x80) ? 0x1F : 0;
        fast.mmu.io[0x0F] = 0;
        fast.mmu.serial_cycles = 4096;
        fast.timer.div_counter = (uint16_t)(scenario * 37);
        ref = fast; ref.fast = NULL; ref.run_mode = GBC_RUN_REFERENCE;
        for (unsigned n = 0; n < 64; n++) {
            unsigned actual = gbc_run_cycles(&fast, (n * 17 % 251) + 1);
            unsigned consumed = 0;
            while (consumed < actual) consumed += gbc_step(&ref);
            expect_int("identical instruction clocks", consumed, actual);
            expect_int("identical CPU", memcmp(&fast.cpu, &ref.cpu, sizeof(ref.cpu)), 0);
            expect_int("identical Timer", memcmp(&fast.timer, &ref.timer, sizeof(ref.timer)), 0);
            expect_int("identical MMU/IRQs", memcmp(&fast.mmu, &ref.mmu, sizeof(ref.mmu)), 0);
            if (test_failures) break;
        }
        gbc_deinit_static(&fast);
        gbc_deinit_static(&ref);
        if (test_failures) break;
    }
}

static void test_audio_events(void)
{
    gbc_t gbc;
    fast_setup(&gbc, 0);
    gbc_apu_write(&gbc, 0x26, 0x80);
    gbc_apu_write(&gbc, 0x24, 0x77); gbc_apu_write(&gbc, 0x25, 0x11);
    gbc_apu_write(&gbc, 0x11, 0x80); gbc_apu_write(&gbc, 0x12, 0xF0);
    gbc_apu_write(&gbc, 0x13, 0); gbc_apu_write(&gbc, 0x14, 0x84); /* period 4096 */
    pcm_frames = pcm_nonzero = 0;
    gbc_apu_fast_step(&gbc, 4096 * 8);
    expect_u8("square period phase", gbc.apu.square1.duty_step, 0);
    expect_u16("square period timer", gbc.apu.square1.timer, 4096);
    for (unsigned i = 0; i < gbc.apu.sample_index * 4U; i++) pcm_nonzero += gbc.apu.sample_buffer[i] != 0;
    expect_int("active PCM", pcm_nonzero != 0, 1);
    gbc.fast->apu_pending = 32;
    uint32_t before = gbc.apu.sample_acc;
    gbc_apu_write(&gbc, 0x26, 0);
    expect_int("register settles time", gbc.apu.sample_acc, (before + 32 * GBC_APU_SAMPLE_RATE) % GBC_CPU_CLOCK_FREQ);
    expect_int("register drains pending", gbc.fast->apu_pending, 0);
    gbc_apu_write(&gbc, 0x26, 0x80);
    expect_u8("re-enable", gbc.apu.enabled, 1);
    expect_u8("trigger required", gbc.apu.square1.enabled, 0);
    gbc_deinit_static(&gbc);
}

static void test_dma_stat_and_palette(void)
{
    gbc_t gbc;
    fast_setup(&gbc, 1);
    gbc.mmu.io[0x41] = 0xE0; gbc.mmu.io[0x45] = 1;
    gbc.mmu.io[0x0F] = 0;
    gbc_ppu_fast_step(&gbc, 4);
    expect_u8("Mode2 STAT IRQ", gbc.mmu.io[0x0F] & GBC_IF_LCD, GBC_IF_LCD);
    gbc_ppu_fast_step(&gbc, 76);
    expect_u8("STAT polling mode3", gbc_mmu_read(&gbc, 0xFF41) & 3, 3);
    gbc_mmu_write(&gbc, 0xFF68, 0x80);
    gbc_mmu_write(&gbc, 0xFF69, 0x1F); gbc_mmu_write(&gbc, 0xFF69, 0);
    expect_int("palette split/autoincrement", gbc.fast->live_bg[0], gbc_ppu_color(31, 0, 0));
    expect_u8("palette index", gbc.ppu.bgpi, 0x82);
    for (unsigned i = 0; i < 16; i++) gbc_mmu_write(&gbc, (uint16_t)(0xC000 + i), (uint8_t)(i + 1));
    gbc_mmu_write(&gbc, 0xFF51, 0xC0); gbc_mmu_write(&gbc, 0xFF52, 0);
    gbc_mmu_write(&gbc, 0xFF53, 0); gbc_mmu_write(&gbc, 0xFF54, 0);
    gbc_mmu_write(&gbc, 0xFF55, 0x80);
    gbc_ppu_fast_step(&gbc, (uint8_t)(gbc.fast->mode3_end - 80));
    expect_u8("HDMA at HBlank", gbc.mmu.vram[0][0], 1);
    expect_u8("HDMA cache dirty", gbc.fast->dirty[0][0], 255);
    gbc_ppu_fast_step(&gbc, (uint8_t)(456 - gbc.fast->mode3_end));
    expect_u8("LY poll", gbc_mmu_read(&gbc, 0xFF44), 1);
    expect_u8("LYC STAT", gbc_mmu_read(&gbc, 0xFF41) & 4, 4);
    gbc_mmu_write(&gbc, 0xFF51, 0xC0); gbc_mmu_write(&gbc, 0xFF52, 0);
    gbc_mmu_write(&gbc, 0xFF53, 0); gbc_mmu_write(&gbc, 0xFF54, 0x10);
    uint16_t pc = gbc.cpu.pc;
    gbc_mmu_write(&gbc, 0xFF55, 0);
    gbc_run_cycles(&gbc, 32);
    expect_u16("GDMA CPU stall", gbc.cpu.pc, pc);
    expect_u8("GDMA output", gbc.mmu.vram[0][16], 1);
    expect_u8("GDMA dirty", gbc.fast->dirty[0][1], 255);
    gbc.mmu.io[0x40] = 0;
    gbc.cpu.pc = 0xFF80;
    gbc.mmu.hram[0] = 0x18; gbc.mmu.hram[1] = 0xFE;
    gbc_mmu_write(&gbc, 0xFF46, 0xC0);
    expect_u8("OAM DMA blocks ROM", gbc_mmu_read(&gbc, 0x100), 255);
    gbc_run_cycles(&gbc, 640);
    expect_u16("RAM instruction execution", gbc.cpu.pc, 0xFF80);
    expect_u16("OAM DMA completed", gbc.mmu.oam_dma_cycles, 0);
    gbc_deinit_static(&gbc);
}

static uint32_t random_state = 17;
static uint8_t random_byte(void)
{
    random_state ^= random_state << 13; random_state ^= random_state >> 17;
    random_state ^= random_state << 5; return (uint8_t)random_state;
}
static void test_random_scanlines(void)
{
    gbc_t fast, ref;
    fast_setup(&fast, 1);
    for (unsigned scene = 0; scene < 10000; scene++) {
        fast.model = (scene & 1) ? GBC_MODEL_CGB : GBC_MODEL_DMG;
        uint8_t ly = random_byte() % 144;
        fast.mmu.io[0x44] = ly;
        for (unsigned i = 0; i < sizeof(fast.mmu.vram); i++) ((uint8_t *)fast.mmu.vram)[i] = random_byte();
        for (unsigned i = 0; i < sizeof(fast.mmu.oam); i++) fast.mmu.oam[i] = random_byte();
        for (unsigned i = 0x40; i < 0x4C; i++) fast.mmu.io[i] = random_byte();
        fast.mmu.io[0x40] |= 0x80; fast.mmu.io[0x44] = ly;
        for (unsigned i = 0; i < 64; i++) {
            fast.ppu.bg_palette[i] = random_byte(); fast.ppu.obj_palette[i] = random_byte();
        }
        fast.ppu.win_line = random_byte(); fast.ppu.dot_cycles = 0;
        gbc_fast_rebuild(&fast);
        ref = fast; ref.fast = NULL; ref.run_mode = GBC_RUN_REFERENCE;
        gbc_ppu_render_scanline(&ref, ly);
        gbc_ppu_fast_step(&fast, 80);
        unsigned remaining = fast.fast->mode3_end - 80;
        gbc_ppu_fast_step(&fast, (uint8_t)remaining);
        expect_int("random fast line", memcmp(fast.ppu.line, ref.ppu.line, sizeof(ref.ppu.line)), 0);
        expect_int("random priority", memcmp(fast.ppu.bg_priority, ref.ppu.bg_priority, sizeof(ref.ppu.bg_priority)), 0);
        expect_int("random IDs", memcmp(fast.ppu.bg_color_id, ref.ppu.bg_color_id, sizeof(ref.ppu.bg_color_id)), 0);
        if (test_failures) break;
    }
    gbc_deinit_static(&fast);
}

int main(void)
{
    core_reference_suite();
    test_budget_and_sync(); test_line_latch_and_cache(); test_audio_cadence_and_lifecycle();
    test_instruction_and_idle_equivalence(); test_audio_events();
    test_dma_stat_and_palette();
    test_random_scanlines();
    printf("fast workspace=%u bytes; fast tests %s\n", (unsigned)sizeof(gbc_fast_t), test_failures ? "FAILED" : "passed");
    return test_failures ? 1 : 0;
}
