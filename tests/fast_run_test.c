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
static uint32_t pcm_hash;
static int callback_switch;
int gbc_sound_output(uint8_t *buffer, size_t bytes)
{
    pcm_frames += (uint32_t)(bytes / 4);
    for (size_t i = 0; i < bytes; i++) {
        pcm_nonzero += buffer[i] != 0;
        pcm_hash = (pcm_hash ^ buffer[i]) * 16777619U;
    }
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

static void test_audio_batch_partition(void)
{
    gbc_t a, b;
    fast_setup(&a, 1);
    gbc_apu_write(&a, 0x26, 0x80);
    gbc_apu_write(&a, 0x24, 0x77); gbc_apu_write(&a, 0x25, 0xFF);
    gbc_apu_write(&a, 0x11, 0x80); gbc_apu_write(&a, 0x12, 0xF3);
    gbc_apu_write(&a, 0x13, 0x81); gbc_apu_write(&a, 0x14, 0x87);
    gbc_apu_write(&a, 0x16, 0x40); gbc_apu_write(&a, 0x17, 0x93);
    gbc_apu_write(&a, 0x18, 0x31); gbc_apu_write(&a, 0x19, 0x85);
    gbc_apu_write(&a, 0x1A, 0x80); gbc_apu_write(&a, 0x1C, 0x20);
    gbc_apu_write(&a, 0x1D, 0x71); gbc_apu_write(&a, 0x1E, 0x86);
    gbc_apu_write(&a, 0x21, 0xE2); gbc_apu_write(&a, 0x22, 0x19);
    gbc_apu_write(&a, 0x23, 0x80);
    b = a; b.fast = NULL;
    pcm_frames = 0; pcm_hash = 2166136261U;
    for (unsigned i = 0; i < 262144; i += 128) gbc_apu_fast_step(&a, 128);
    uint32_t count = pcm_frames, hash = pcm_hash;
    pcm_frames = 0; pcm_hash = 2166136261U;
    for (unsigned i = 0; i < 262144; i += 512) gbc_apu_fast_step(&b, 512);
    expect_int("batch PCM count", pcm_frames, count);
    expect_int("batch PCM bytes", pcm_hash == hash, 1);
    expect_int("batch APU phase/state", memcmp(&a.apu, &b.apu, sizeof(a.apu)), 0);
    gbc_deinit_static(&a);
}

static void test_audio_sequencer_boundary(void)
{
    static gbc_t fast, unit;
    for (unsigned negative = 0; negative < 2; negative++) {
        fast_setup(&fast, 1);
        gbc_apu_write(&fast, 0x26, 0x80);
        gbc_apu_write(&fast, 0x24, 0x77); gbc_apu_write(&fast, 0x25, 0x11);
        gbc_apu_write(&fast, 0x10, (uint8_t)(0x11 | (negative ? 8 : 0)));
        gbc_apu_write(&fast, 0x11, 0x80); gbc_apu_write(&fast, 0x12, 0xF0);
        gbc_apu_write(&fast, 0x13, 0xE8); gbc_apu_write(&fast, 0x14, 0x83); /* 1000 */
        fast.apu.frame_step = 2;
        fast.apu.frame_acc = GBC_CPU_CLOCK_FREQ - 100U * 512U;
        fast.apu.sample_acc = 0;
        fast.apu.square1.timer = 10;
        fast.apu.square1.sweep_timer = 1;
        unit = fast; unit.fast = NULL; unit.run_mode = GBC_RUN_REFERENCE;
        gbc_apu_fast_step(&fast, 100);
        for (unsigned i = 0; i < 100; i++) gbc_apu_step(&unit, 1);
        /* The timer reload at clock 10 uses frequency 1000; a sweep at
         * clock 100 must not retroactively change that reload's period. */
        expect_u16("sweep preserves earlier timer reload", fast.apu.square1.timer, 4102);
        expect_int("sweep boundary unit state", memcmp(&fast.apu, &unit.apu, sizeof(fast.apu)), 0);
        pcm_frames = 0; pcm_hash = 2166136261U;
        for (unsigned i = 0; i < 262144; i += 512) gbc_apu_fast_step(&fast, 512);
        uint32_t count = pcm_frames, hash = pcm_hash;
        pcm_frames = 0; pcm_hash = 2166136261U;
        for (unsigned i = 0; i < 262144; i++) gbc_apu_step(&unit, 1);
        expect_int("sweep unit PCM count", pcm_frames, count);
        expect_int("sweep unit PCM bytes", pcm_hash == hash, 1);
        expect_int("sweep unit phase/state", memcmp(&fast.apu, &unit.apu, sizeof(fast.apu)), 0);
        gbc_deinit_static(&fast);
    }
}

static void test_audio_all_channel_unit_clock(void)
{
    static gbc_t fast, unit;
    static const uint8_t steps[] = {0, 2, 7}; /* length, sweep, envelope */
    for (unsigned scenario = 0; scenario < sizeof(steps); scenario++) {
        fast_setup(&fast, 1);
        gbc_apu_write(&fast, 0x26, 0x80);
        gbc_apu_write(&fast, 0x24, 0x77); gbc_apu_write(&fast, 0x25, 0xFF);
        gbc_apu_write(&fast, 0x10, 0x19);
        gbc_apu_write(&fast, 0x11, 0x80); gbc_apu_write(&fast, 0x12, 0xF1);
        gbc_apu_write(&fast, 0x13, 0xE8); gbc_apu_write(&fast, 0x14, 0xC3);
        gbc_apu_write(&fast, 0x16, 0x40); gbc_apu_write(&fast, 0x17, 0x91);
        gbc_apu_write(&fast, 0x18, 0x71); gbc_apu_write(&fast, 0x19, 0xC6);
        for (unsigned i = 0; i < 16; i++) fast.mmu.io[0x30 + i] = (uint8_t)(i * 17);
        gbc_apu_write(&fast, 0x1A, 0x80); gbc_apu_write(&fast, 0x1C, 0x20);
        gbc_apu_write(&fast, 0x1D, 0x91); gbc_apu_write(&fast, 0x1E, 0xC7);
        gbc_apu_write(&fast, 0x21, 0xE9); gbc_apu_write(&fast, 0x22, 0x09);
        gbc_apu_write(&fast, 0x23, 0xC0);
        fast.apu.frame_step = steps[scenario];
        fast.apu.frame_acc = GBC_CPU_CLOCK_FREQ - 173U * 512U;
        fast.apu.sample_acc = GBC_CPU_CLOCK_FREQ - 61U * GBC_APU_SAMPLE_RATE;
        if (!scenario) {
            fast.apu.square1.length_counter = fast.apu.square2.length_counter = 1;
            fast.apu.wave.length_counter = fast.apu.noise.length_counter = 1;
        }
        unit = fast; unit.fast = NULL; unit.run_mode = GBC_RUN_REFERENCE;
        pcm_frames = 0; pcm_hash = 2166136261U;
        for (unsigned i = 0; i < 131072; i += 512) gbc_apu_fast_step(&fast, 512);
        uint32_t count = pcm_frames, hash = pcm_hash;
        pcm_frames = 0; pcm_hash = 2166136261U;
        for (unsigned i = 0; i < 131072; i++) gbc_apu_step(&unit, 1);
        expect_int("all-channel unit PCM count", pcm_frames, count);
        expect_int("all-channel unit PCM bytes", pcm_hash == hash, 1);
        expect_int("all-channel unit phase/state", memcmp(&fast.apu, &unit.apu, sizeof(fast.apu)), 0);
        expect_int("all-channel unit IO", memcmp(fast.mmu.io, unit.mmu.io, sizeof(fast.mmu.io)), 0);
        gbc_deinit_static(&fast);
    }
}

static void test_late_ly_write_selection(void)
{
    gbc_t fast, ref;
    fast_setup(&fast, 0);
    gbc_mmu_write(&fast, 0xFF40, 0x93);
    gbc_mmu_write(&fast, 0xFF47, 0xE4);
    gbc_mmu_write(&fast, 0xFF48, 0xE4);
    fast.mmu.io[0x44] = 8;
    fast.mmu.oam[0] = 16; fast.mmu.oam[1] = 8;
    fast.mmu.oam[2] = 1; fast.mmu.oam[3] = 0;
    for (unsigned y = 0; y < 8; y++) gbc_mmu_write(&fast, (uint16_t)(0x8010 + y * 2), 0xFF);
    gbc_ppu_fast_step(&fast, 80);
    /* Preserve the core's existing LY-write behavior even during Mode 3. */
    gbc_mmu_write(&fast, 0xFF44, 0);
    ref = fast; ref.fast = NULL; ref.run_mode = GBC_RUN_REFERENCE;
    gbc_ppu_render_scanline(&ref, 0);
    gbc_ppu_fast_step(&fast, (uint8_t)(fast.fast->mode3_end - 80));
    expect_int("LY write reselects sprites", memcmp(fast.ppu.line, ref.ppu.line, sizeof(ref.ppu.line)), 0);
    gbc_deinit_static(&fast);
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

static void test_ppu_inline_equivalence(void)
{
    static gbc_t a, b;
    static const uint8_t registers[] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x47, 0x4A, 0x4B};
    for (unsigned model = 0; model < 2; model++) {
        fast_setup(&a, (uint8_t)model); fast_setup(&b, (uint8_t)model);
        for (unsigned i = 0; i < sizeof(a.mmu.vram); i++)
            ((uint8_t *)a.mmu.vram)[i] = ((uint8_t *)b.mmu.vram)[i] = random_byte();
        for (unsigned i = 0; i < sizeof(a.mmu.oam); i++)
            a.mmu.oam[i] = b.mmu.oam[i] = random_byte();
        for (unsigned i = 0; i < 30000; i++) {
            if ((i & 31) == 0) {
                uint8_t reg = registers[random_byte() % sizeof(registers)];
                uint8_t data = random_byte();
                gbc_mmu_write(&a, (uint16_t)(0xFF00 | reg), data);
                gbc_mmu_write(&b, (uint16_t)(0xFF00 | reg), data);
            }
            if ((i & 511) == 0) { gbc_fast_rebuild(&a); gbc_fast_rebuild(&b); }
            uint8_t cycles = random_byte();
            gbc_fast_ppu_advance(&a, cycles); gbc_ppu_fast_step(&b, cycles);
            expect_int("inline PPU IO", memcmp(a.mmu.io, b.mmu.io, sizeof(a.mmu.io)), 0);
            expect_int("inline PPU state", memcmp(&a.ppu, &b.ppu, sizeof(a.ppu)), 0);
            if ((i & 127) == 0) {
                expect_int("inline PPU derived", memcmp(a.fast, b.fast, sizeof(*a.fast)), 0);
                expect_int("inline PPU pixels", memcmp(a.gbc_draw_data, b.gbc_draw_data, sizeof(a.gbc_draw_data)), 0);
            }
        }
        expect_int("inline PPU unload a", gbc_unload_rom_discard(&a), GBC_OK);
        expect_int("inline PPU unload b", gbc_unload_rom_discard(&b), GBC_OK);
        gbc_deinit_static(&a); gbc_deinit_static(&b);
    }
}
static void test_idle_peripheral_equivalence(void)
{
    static gbc_t a, b;
    fast_setup(&a, GBC_MODEL_CGB); fast_setup(&b, GBC_MODEL_CGB);
    for (unsigned i = 0; i < 30000; i++) {
        a.timer.div_counter = (uint16_t)((random_byte() << 8) | random_byte());
        a.timer.tima_reload_delay = (i & 7) ? 0 : (uint8_t)(1 + (i & 3));
        a.mmu.io[0x07] = random_byte() & 7;
        a.mmu.io[0x04] = random_byte(); /* zero clocks must leave stale DIV alone */
        a.mmu.io[0x05] = random_byte(); a.mmu.io[0x06] = random_byte();
        a.mmu.io[0x0F] = random_byte();
        a.mmu.oam_dma_cycles = (i & 1) ? 0 : random_byte();
        a.mmu.serial_cycles = (i & 2) ? 0 : random_byte();
        /* Cover HDMA retirement and the inactive blocks-with-zero-budget case. */
        a.mmu.hdma_busy_cycles = (i & 3) ? 0 : (uint8_t)(1 + (i & 15));
        a.mmu.hdma_blocks = 0;
        a.mmu.hdma_src = 0; a.mmu.hdma_dst = 0;
        a.cpu.halted = random_byte() & 1;
        b.timer = a.timer; b.mmu = a.mmu; b.cpu = a.cpu;
        uint8_t clocks = random_byte();
        if (a.mmu.oam_dma_cycles || a.mmu.hdma_busy_cycles) gbc_mmu_step_dma(&a, clocks);
        if (a.mmu.serial_cycles) gbc_mmu_step_serial(&a, clocks);
        gbc_fast_timer_advance(&a, clocks);
        gbc_mmu_step_dma(&b, clocks); gbc_mmu_step_serial(&b, clocks); gbc_timer_step(&b, clocks);
        expect_int("idle peripherals timer", memcmp(&a.timer, &b.timer, sizeof(a.timer)), 0);
        expect_int("idle peripherals MMU", memcmp(&a.mmu, &b.mmu, sizeof(a.mmu)), 0);
        expect_int("idle peripherals CPU", memcmp(&a.cpu, &b.cpu, sizeof(a.cpu)), 0);
    }
    gbc_unload_rom_discard(&a); gbc_unload_rom_discard(&b);
    gbc_deinit_static(&a); gbc_deinit_static(&b);
}
int main(void)
{
    core_reference_suite();
    test_budget_and_sync(); test_line_latch_and_cache(); test_audio_cadence_and_lifecycle();
    test_instruction_and_idle_equivalence(); test_audio_events();
    test_dma_stat_and_palette();
    test_audio_batch_partition();
    test_audio_sequencer_boundary();
    test_audio_all_channel_unit_clock();
    test_late_ly_write_selection();
    test_random_scanlines();
    test_ppu_inline_equivalence();
    test_idle_peripheral_equivalence();
    printf("fast workspace=%u bytes; fast tests %s\n", (unsigned)sizeof(gbc_fast_t), test_failures ? "FAILED" : "passed");
    return test_failures ? 1 : 0;
}
