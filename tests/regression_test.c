/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdarg.h>
#include <stdlib.h>

#include "gbc.h"

static uint32_t test_ticks;
static int test_failures;
static int test_fail_allocation;
static int test_frees;

void *gbc_malloc(int num)
{
    return test_fail_allocation ? NULL : malloc((size_t)num);
}

void gbc_free(void *address)
{
    test_frees++;
    free(address);
}

void *gbc_memcpy(void *str1, const void *str2, size_t n)
{
    return memcpy(str1, str2, n);
}

void *gbc_memset(void *str, int c, size_t n)
{
    return memset(str, c, n);
}

int gbc_memcmp(const void *str1, const void *str2, size_t n)
{
    return memcmp(str1, str2, n);
}

FILE *gbc_fopen(const char *filename, const char *mode)
{
    return fopen(filename, mode);
}

size_t gbc_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return fread(ptr, size, nmemb, stream);
}

size_t gbc_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return fwrite(ptr, size, nmemb, stream);
}

int gbc_fseek(FILE *stream, long int offset, int whence)
{
    return fseek(stream, offset, whence);
}

long gbc_ftell(FILE *stream)
{
    return ftell(stream);
}

int gbc_fclose(FILE *stream)
{
    return fclose(stream);
}

int gbc_draw(int x1, int y1, int x2, int y2, gbc_color_t *color_data)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color_data;
    return GBC_OK;
}

int gbc_sound_output(uint8_t *buffer, size_t len)
{
    (void)buffer;
    (void)len;
    return GBC_OK;
}

int gbc_log_printf(const char *format, ...)
{
    (void)format;
    return 0;
}

uint32_t gbc_get_ticks(void)
{
    return test_ticks;
}

void gbc_delay(uint32_t ms)
{
    test_ticks += ms;
}

int gbc_initex(gbc_t *gbc)
{
    (void)gbc;
    return GBC_OK;
}

int gbc_deinitex(gbc_t *gbc)
{
    (void)gbc;
    return GBC_OK;
}

void gbc_frame(gbc_t *gbc)
{
    (void)gbc;
}

static void expect_u8(const char *name, uint8_t actual, uint8_t expected)
{
    if (actual != expected) {
        printf("FAIL %s: got 0x%02X expected 0x%02X\n", name, actual, expected);
        test_failures++;
    }
}

static void expect_u16(const char *name, uint16_t actual, uint16_t expected)
{
    if (actual != expected) {
        printf("FAIL %s: got 0x%04X expected 0x%04X\n", name, actual, expected);
        test_failures++;
    }
}

static void expect_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        printf("FAIL %s: got %d expected %d\n", name, actual, expected);
        test_failures++;
    }
}

static void fill_test_rom(uint8_t *rom, uint8_t cart_type, uint8_t ram_size_code)
{
    gbc_memset(rom, 0, 32768);
    gbc_memcpy(&rom[0x134], "GBCREGRESSION", 13);
    rom[0x147] = cart_type;
    rom[0x148] = 0x00;
    rom[0x149] = ram_size_code;
}

static void test_halt_bug_reuses_next_opcode_byte(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));
    rom[0x0100] = 0x76; /* HALT */
    rom[0x0101] = 0x3E; /* LD A,d8 */
    rom[0x0102] = 0x42;
    gbc.cart.rom_data = rom;
    gbc.cpu.pc = 0x0100;
    gbc.mmu.ie = GBC_IF_VBLANK;
    gbc.mmu.io[0x0F] = GBC_IF_VBLANK;

    expect_u8("halt bug halt cycles", gbc_cpu_step(&gbc), 4);
    expect_u8("halt bug does not stay halted", gbc.cpu.halted, 0);
    expect_u16("halt bug pc after halt", gbc.cpu.pc, 0x0101);

    expect_u8("halt bug next instruction cycles", gbc_cpu_step(&gbc), 8);
    expect_u8("halt bug duplicated opcode as immediate", gbc.cpu.af.hi, 0x3E);
    expect_u16("halt bug pc after duplicated read", gbc.cpu.pc, 0x0102);
}

static void test_halt_keeps_cpu_stopped_while_ime_clear(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));
    rom[0x0100] = 0xFB; /* EI */
    rom[0x0101] = 0x76; /* HALT */
    rom[0x0102] = 0x3C; /* INC A - must never run */
    rom[0x0103] = 0x3C;
    rom[0x0104] = 0x3C;
    gbc.cart.rom_data = rom;
    gbc.cpu.pc = 0x0100;
    gbc.cpu.sp = 0xFFFE; /* the push parks the return address at 0xFFFC/0xFFFD */
    gbc.mmu.ie = GBC_IF_VBLANK;
    gbc.mmu.io[0x0F] = 0x00; /* nothing pending while EI and HALT run */

    /* EI only schedules IME: it must stay pending until after the next
     * instruction fetch, so a HALT right behind the EI is still protected. */
    expect_u8("ei cycles", gbc_cpu_step(&gbc), 4);
    expect_u8("ei schedules ime", gbc.cpu.ime_delay, 1);

    expect_u8("halt cycles", gbc_cpu_step(&gbc), 4);
    expect_u8("ime becomes effective after the halt", gbc.cpu.ime, 1);
    expect_u8("halt stops cpu", gbc.cpu.halted, 1);
    expect_u8("halt did not trigger the halt bug", gbc.cpu.halt_bug, 0);
    /* The pc has already stepped past the halt opcode; the cpu must stay
     * there until an enabled interrupt actually arrives. */
    expect_u16("halt leaves pc", gbc.cpu.pc, 0x0102);
    expect_u8("halt did not run the next instruction", gbc.cpu.af.hi, 0x00);

    /* The vblank now arrives while the CPU is stopped.  It must wake the
     * CPU and be serviced, without executing the instruction the halt was
     * protecting against. */
    gbc.mmu.io[0x0F] = GBC_IF_VBLANK;
    expect_u8("vblank service cycles", gbc_cpu_step(&gbc), 20);
    expect_u8("ime cleared while servicing", gbc.cpu.ime, 0);
    expect_u8("halt cleared by service", gbc.cpu.halted, 0);
    expect_u8("ime delay consumed by service", gbc.cpu.ime_delay, 0);
    expect_u16("pc jumps to vblank vector", gbc.cpu.pc, 0x0040);
    expect_u16("service pushes the instruction after halt",
               (uint16_t)((uint16_t)gbc.mmu.hram[0x7C] | ((uint16_t)gbc.mmu.hram[0x7D] << 8)), 0x0102);
    expect_u8("halted cpu did not run inc a", gbc.cpu.af.hi, 0x00);
}

static void test_halt_wakes_on_pending_irq_while_ime_is_off(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));
    rom[0x0100] = 0x76; /* HALT, with interrupts globally disabled */
    gbc.cart.rom_data = rom;
    gbc.cpu.pc = 0x0100;
    gbc.cpu.sp = 0xFFFE;
    gbc.mmu.ie = GBC_IF_VBLANK; /* the flag is enabled but nothing is pending yet */

    expect_u8("halt without ime stops cpu", gbc_cpu_step(&gbc), 4);
    expect_u8("cpu stopped with interrupts off", gbc.cpu.halted, 1);

    /* An enabled pending IRQ wakes HALT even with IME clear. */
    gbc.mmu.io[0x0F] = GBC_IF_VBLANK;
    expect_u8("pending IRQ wakes HALT without service", gbc_cpu_step(&gbc), 4);
    expect_u8("IME-off CPU resumes", gbc.cpu.halted, 0);
    expect_u8("wake does not enable IME", gbc.cpu.ime, 0);
    expect_u16("wake executes next opcode", gbc.cpu.pc, 0x0102);
    expect_u8("pending IRQ remains unserviced", gbc.mmu.io[0x0F], GBC_IF_VBLANK);

}

static void test_halt_bug_without_ime_runs_next_instruction(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));
    rom[0x0100] = 0x76; /* HALT */
    rom[0x0101] = 0x3C; /* INC A - the byte the halt bug re-runs */
    gbc.cart.rom_data = rom;
    gbc.cpu.pc = 0x0100;
    gbc.cpu.sp = 0xFFFE;
    gbc.mmu.ie = GBC_IF_VBLANK;
    gbc.mmu.io[0x0F] = GBC_IF_VBLANK; /* pending with IME clear */

    expect_u8("halt with pending irq and ime off", gbc_cpu_step(&gbc), 4);
    expect_u8("halt bug latched", gbc.cpu.halt_bug, 1);
    expect_u8("halt bug does not stop cpu", gbc.cpu.halted, 0);

    /* The halt bug re-reads the byte the pc already points at, so the next
     * instruction runs and only then does the pc move on. */
    expect_u8("halt bug step cycles", gbc_cpu_step(&gbc), 4);
    expect_u8("halt bug re-ran the byte", gbc.cpu.af.hi, 0x01);
    expect_u8("halt bug cleared", gbc.cpu.halt_bug, 0);
    expect_u16("halt bug leaves pc at the re-run byte", gbc.cpu.pc, 0x0101);
}

static void test_mbc3_rtc_registers_count_and_latch(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));
    gbc.cart.rom_data = rom;
    gbc.cart.header.mbc = GBC_MBC3;
    gbc.cart.header.has_timer = 1;
    test_ticks = 1000;

    gbc_cart_write(&gbc, 0x0000, 0x0A);
    gbc_cart_write(&gbc, 0x4000, 0x08);
    gbc_cart_write_ram(&gbc, 0xA000, 58);
    test_ticks += 2000;
    gbc_cart_write(&gbc, 0x6000, 0x00);
    gbc_cart_write(&gbc, 0x6000, 0x01);

    expect_u8("mbc3 rtc seconds select", gbc_cart_read_ram(&gbc, 0xA000), 0);
    gbc_cart_write(&gbc, 0x4000, 0x09);
    expect_u8("mbc3 rtc minute carry", gbc_cart_read_ram(&gbc, 0xA000), 1);
}

static void test_battery_ram_survives_reload(void)
{
    static const char *rom_path = "gbc_regression_test.gb";
    static const char *save_path = "gbc_regression_test.gb.sav";
    static uint8_t rom[32768];
    gbc_t *gbc;
    FILE *fp;

    remove(rom_path);
    remove(save_path);
    fill_test_rom(rom, 0x03, 0x02);

    fp = fopen(rom_path, "wb");
    if (fp == NULL) {
        printf("FAIL create rom file\n");
        test_failures++;
        return;
    }
    if (fwrite(rom, 1, sizeof(rom), fp) != sizeof(rom)) {
        printf("FAIL write rom file\n");
        test_failures++;
        fclose(fp);
        return;
    }
    fclose(fp);

    gbc = gbc_init();
    if (gbc == NULL) {
        printf("FAIL init gbc\n");
        test_failures++;
        return;
    }
    expect_int("battery first load", gbc_load_file(gbc, rom_path), GBC_OK);
    gbc_cart_write(gbc, 0x0000, 0x0A);
    gbc_cart_write_ram(gbc, 0xA123, 0x5A);
    expect_int("battery first unload", gbc_unload_file(gbc), GBC_OK);
    expect_int("battery deinit", gbc_deinit(gbc), GBC_OK);

    gbc = gbc_init();
    if (gbc == NULL) {
        printf("FAIL reinit gbc\n");
        test_failures++;
        return;
    }
    expect_int("battery second load", gbc_load_file(gbc, rom_path), GBC_OK);
    gbc_cart_write(gbc, 0x0000, 0x0A);
    expect_u8("battery restored byte", gbc_cart_read_ram(gbc, 0xA123), 0x5A);
    expect_int("battery second deinit", gbc_deinit(gbc), GBC_OK);

    remove(rom_path);
    remove(save_path);
}

static void test_timer_div_write_falling_edge_increments_tima(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.timer.div_counter = 0x0200;
    gbc.mmu.io[0x05] = 0x12;
    gbc.mmu.io[0x07] = 0x04;

    gbc_timer_write_div(&gbc);

    expect_u16("timer div internal reset", gbc.timer.div_counter, 0x0000);
    expect_u8("timer div register reset", gbc.mmu.io[0x04], 0x00);
    expect_u8("timer div falling edge increments tima", gbc.mmu.io[0x05], 0x13);
}

static void test_timer_tac_write_falling_edge_increments_tima(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.timer.div_counter = 0x0200;
    gbc.mmu.io[0x05] = 0x20;
    gbc.mmu.io[0x07] = 0x04;

    gbc_mmu_write(&gbc, 0xFF07, 0x00);

    expect_u8("timer tac falling edge increments tima", gbc.mmu.io[0x05], 0x21);
    expect_u8("timer tac write stores value", gbc.mmu.io[0x07], 0x00);
}

static void test_timer_overflow_delays_reload_and_interrupt(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.timer.div_counter = 0x000C;
    gbc.mmu.io[0x05] = 0xFF;
    gbc.mmu.io[0x06] = 0xAB;
    gbc.mmu.io[0x07] = 0x05;
    gbc.mmu.io[0x0F] = 0xE0;

    gbc_timer_step(&gbc, 4);
    expect_u8("timer overflow leaves tima zero", gbc.mmu.io[0x05], 0x00);
    expect_u8("timer overflow interrupt delayed", gbc.mmu.io[0x0F] & GBC_IF_TIMER, 0x00);

    gbc_timer_step(&gbc, 4);
    expect_u8("timer overflow reloads tma", gbc.mmu.io[0x05], 0xAB);
    expect_u8("timer overflow requests interrupt", gbc.mmu.io[0x0F] & GBC_IF_TIMER, GBC_IF_TIMER);
}

static void test_dmg_sprite_lower_x_has_priority_over_oam_order(void)
{
    gbc_t gbc;
    gbc_color_t expected;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.model = GBC_MODEL_DMG;
    gbc.mmu.io[0x40] = 0x82;
    gbc.mmu.io[0x48] = 0x04;
    gbc.mmu.io[0x49] = 0x08;
    gbc.mmu.vram[0][0] = 0xFF;
    gbc.mmu.vram[0][16] = 0xFF;

    gbc.mmu.oam[0] = 16;
    gbc.mmu.oam[1] = 18;
    gbc.mmu.oam[2] = 0;
    gbc.mmu.oam[3] = 0x00;
    gbc.mmu.oam[4] = 16;
    gbc.mmu.oam[5] = 17;
    gbc.mmu.oam[6] = 1;
    gbc.mmu.oam[7] = 0x10;

    gbc_ppu_render_scanline(&gbc, 0);
    expected = gbc_ppu_color(10, 10, 10);

    if (gbc.gbc_draw_data[10] != expected) {
        printf("FAIL dmg sprite x priority: got 0x%08X expected 0x%08X\n",
               (unsigned)gbc.gbc_draw_data[10], (unsigned)expected);
        test_failures++;
    }
}

static void test_hblank_hdma_copies_one_block_per_hblank(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.model = GBC_MODEL_CGB;
    gbc.mmu.io[0x40] = 0x80;
    gbc.mmu.io[0x41] = 0x83;
    gbc.ppu.mode = 3;
    gbc.ppu.dot_cycles = 251;
    for (uint8_t i = 0; i < 32; i++) {
        gbc.mmu.wram[0][i] = (uint8_t)(0xA0 + i);
    }

    gbc_mmu_write(&gbc, 0xFF51, 0xC0);
    gbc_mmu_write(&gbc, 0xFF52, 0x00);
    gbc_mmu_write(&gbc, 0xFF53, 0x00);
    gbc_mmu_write(&gbc, 0xFF54, 0x00);
    gbc_mmu_write(&gbc, 0xFF55, 0x81);

    expect_u8("hdma active after start", gbc_mmu_read(&gbc, 0xFF55), 0x01);
    expect_u8("hdma does not copy immediately", gbc.mmu.vram[0][0], 0x00);

    gbc_ppu_step(&gbc, 1);
    expect_u8("hdma first block byte 0", gbc.mmu.vram[0][0], 0xA0);
    expect_u8("hdma first block byte 15", gbc.mmu.vram[0][15], 0xAF);
    expect_u8("hdma waits for second hblank", gbc.mmu.vram[0][16], 0x00);
    expect_u8("hdma one block remains", gbc_mmu_read(&gbc, 0xFF55), 0x00);

    gbc_ppu_step(&gbc, 204);
    gbc_ppu_step(&gbc, 252);
    expect_u8("hdma second block byte 16", gbc.mmu.vram[0][16], 0xB0);
    expect_u8("hdma complete", gbc_mmu_read(&gbc, 0xFF55), 0xFF);
}

static void test_general_purpose_hdma_stalls_cpu_until_finished(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));  /* every byte is NOP */
    gbc.model = GBC_MODEL_CGB;
    gbc.cart.rom_data = rom;
    gbc.cpu.pc = 0x0100;
    for (uint8_t i = 0; i < 32; i++) {
        gbc.mmu.wram[0][i] = (uint8_t)(0xA0 + i);
    }

    gbc_mmu_write(&gbc, 0xFF51, 0xC0);
    gbc_mmu_write(&gbc, 0xFF52, 0x00);
    gbc_mmu_write(&gbc, 0xFF53, 0x00);
    gbc_mmu_write(&gbc, 0xFF54, 0x00);
    gbc_mmu_write(&gbc, 0xFF55, 0x01);  /* general purpose, two blocks */

    expect_u8("general hdma not copied at start", gbc.mmu.vram[0][0], 0x00);
    expect_u8("general hdma reads ff while busy", gbc_mmu_read(&gbc, 0xFF55), 0xFF);

    /* Thirty-two cycles per block means the CPU cannot fetch during the transfer. */
    expect_u8("general hdma cpu is stalled", gbc_mmu_cpu_stalled(&gbc), 1);
    expect_u8("general hdma stalls the cpu step", gbc_cpu_step(&gbc), 4);
    expect_u16("general hdma cpu does not advance", gbc.cpu.pc, 0x0100);

    gbc_mmu_step_dma(&gbc, 31);
    expect_u8("first block waits for its full budget", gbc.mmu.vram[0][0], 0x00);
    gbc_mmu_step_dma(&gbc, 1);
    expect_u8("first block lands after 32 cycles", gbc.mmu.vram[0][0], 0xA0);
    expect_u8("second block is not copied with the first", gbc.mmu.vram[0][16], 0x00);
    expect_u8("general hdma still busy", gbc_mmu_cpu_stalled(&gbc), 1);

    gbc_mmu_step_dma(&gbc, 32);
    expect_u8("second block lands after another 32", gbc.mmu.vram[0][16], 0xB0);
    expect_u8("general hdma releases the bus", gbc_mmu_cpu_stalled(&gbc), 0);
    expect_u8("general hdma reports completion", gbc_mmu_read(&gbc, 0xFF55), 0xFF);

    gbc.cpu.pc = 0x0100;
    expect_u8("cpu runs again after the transfer", gbc_cpu_step(&gbc), 4);
    expect_u16("cpu fetched after the transfer", gbc.cpu.pc, 0x0101);
}

static void test_general_purpose_hdma_length_scales_with_blocks(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.model = GBC_MODEL_CGB;
    /* One block advances the source by 16 bytes, so the fourth block reads
     * from 0xC030 and lands at 0x8030 (offset 32 of VRAM bank 0). */
    gbc.mmu.wram[0][0] = 0x5A;
    gbc.mmu.wram[0][0x30] = 0xA5;

    gbc_mmu_write(&gbc, 0xFF51, 0xC0);
    gbc_mmu_write(&gbc, 0xFF52, 0x00);
    gbc_mmu_write(&gbc, 0xFF53, 0x00);
    gbc_mmu_write(&gbc, 0xFF54, 0x00);
    gbc_mmu_write(&gbc, 0xFF55, 0x03);  /* general purpose, four blocks */

    /* Three blocks fit in 96 cycles exactly, so the fourth is still in
     * flight and the bus is still held. */
    gbc_mmu_step_dma(&gbc, 96);
    expect_u8("four block transfer still running", gbc_mmu_cpu_stalled(&gbc), 1);
    expect_u8("four block transfer copied three blocks", gbc.mmu.vram[0][32], 0x00);
    expect_u8("four block transfer copied the first block", gbc.mmu.vram[0][0], 0x5A);

    gbc_mmu_step_dma(&gbc, 32);
    expect_u8("four block transfer released the bus", gbc_mmu_cpu_stalled(&gbc), 0);
    expect_u8("four block transfer copied the last block", gbc.mmu.vram[0][0x30], 0xA5);
    expect_u8("hdma5 registers advance to the end", gbc_mmu_read(&gbc, 0xFF51), 0xC0);
    expect_u8("hdma5 low source nibble advances", gbc_mmu_read(&gbc, 0xFF52), 0x40);
}

static void test_oam_dma_blocks_non_hram_until_transfer_finishes(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    for (uint8_t i = 0; i < 0xA0; i++) {
        gbc.mmu.wram[0][i] = (uint8_t)(0x40 + i);
    }
    gbc.mmu.hram[0] = 0x24;

    gbc_mmu_write(&gbc, 0xFF46, 0xC0);
    expect_u8("oam dma copies immediately", gbc.mmu.oam[0], 0x40);
    expect_u8("oam dma blocks wram read", gbc_mmu_read(&gbc, 0xC000), 0xFF);
    expect_u8("oam dma allows hram read", gbc_mmu_read(&gbc, 0xFF80), 0x24);

    gbc_mmu_write(&gbc, 0xC000, 0x77);
    gbc_mmu_write(&gbc, 0xFF80, 0x55);
    expect_u8("oam dma blocks wram write", gbc.mmu.wram[0][0], 0x40);
    expect_u8("oam dma allows hram write", gbc.mmu.hram[0], 0x55);

    gbc_mmu_step_dma(&gbc, 639);
    expect_u8("oam dma still blocks before final cycle", gbc_mmu_read(&gbc, 0xC000), 0xFF);
    gbc_mmu_step_dma(&gbc, 1);
    expect_u8("oam dma unblocks after transfer", gbc_mmu_read(&gbc, 0xC000), 0x40);
}

static void test_mbc2_loads_banks_and_internal_4bit_ram(void)
{
    gbc_t gbc;
    static uint8_t rom[65536];

    gbc_memset(&gbc, 0, sizeof(gbc));
    fill_test_rom(rom, 0x06, 0x00);
    rom[0x148] = 0x01;
    rom[0x4000] = 0x11;
    rom[0x8000] = 0x22;
    rom[0xC000] = 0x33;

    expect_int("mbc2 load rom", gbc_cart_load_rom(&gbc, rom, sizeof(rom), 0), GBC_OK);
    expect_u8("mbc2 initial bank", gbc_cart_read_rom(&gbc, 0x4000), 0x11);

    gbc_cart_write(&gbc, 0x2100, 0x02);
    expect_u8("mbc2 switched bank", gbc_cart_read_rom(&gbc, 0x4000), 0x22);

    gbc_cart_write(&gbc, 0x0000, 0x0A);
    gbc_cart_write_ram(&gbc, 0xA123, 0xBC);
    expect_u8("mbc2 internal ram lower nibble", gbc_cart_read_ram(&gbc, 0xA123), 0xFC);
    expect_u8("mbc2 internal ram mirrors 512 bytes", gbc_cart_read_ram(&gbc, 0xA323), 0xFC);

    gbc_cart_unload(&gbc);
}

static void test_largest_rom_size_code_reports_512_banks(void)
{
    gbc_t gbc;
    uint8_t *rom;

    /* Size code 0x08 is the largest the header can describe: 8 MiB, which is
     * 512 banks.  The bank count has to survive as a uint16_t and the top bank
     * has to be reachable through the MBC5 bank register. */
    rom = (uint8_t *)gbc_malloc(8388608);
    if (rom == NULL) {
        printf("FAIL allocate 8MiB test rom\n");
        test_failures++;
        return;
    }

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, 8388608);
    gbc_memcpy(&rom[0x134], "GBCREGRESSION", 13);
    rom[0x143] = 0x80; /* CGB compatible */
    rom[0x147] = 0x19; /* MBC5 */
    rom[0x148] = 0x08; /* 8 MiB */
    rom[0x149] = 0x00;
    /* The reset vector comes from the mapper, so these bytes are only
     * reachable if bank 0 is mapped at 0x0000. */
    rom[0x0100] = 0x00; /* NOP */
    rom[0x0101] = 0xC3; /* JP */
    rom[0x7FC000] = 0x5A; /* last 16 KiB bank, first byte */

    expect_int("8MiB rom load", gbc_cart_load_rom(&gbc, rom, 8388608, 0), GBC_OK);
    expect_int("8MiB rom size", (int)gbc.cart.header.rom_size, 8388608);
    expect_int("8MiB rom banks", (int)gbc.cart.header.rom_banks, 512);
    expect_u8("8MiB reset vector readable", gbc_cart_read_rom(&gbc, 0x0100), 0x00);
    expect_u8("8MiB reset vector second byte", gbc_cart_read_rom(&gbc, 0x0101), 0xC3);

    gbc_cart_write(&gbc, 0x2000, 0xFF);
    gbc_cart_write(&gbc, 0x3000, 0x01); /* bank 511 = 0x1FF */
    expect_int("8MiB high bank selected", (int)gbc.cart.rom_bank, 511);
    expect_u8("8MiB high bank readable", gbc_cart_read_rom(&gbc, 0x4000), 0x5A);

    gbc_cart_unload(&gbc);
    gbc_free(rom);
}

static void test_mbc2_battery_ram_survives_reload(void)
{
    static const char *rom_path = "gbc_mbc2_test.gb";
    static const char *save_path = "gbc_mbc2_test.gb.sav";
    static uint8_t rom[32768];
    gbc_t *gbc;
    FILE *fp;

    remove(rom_path);
    remove(save_path);
    fill_test_rom(rom, 0x06, 0x00);

    fp = fopen(rom_path, "wb");
    if (fp == NULL) {
        printf("FAIL create mbc2 rom file\n");
        test_failures++;
        return;
    }
    if (fwrite(rom, 1, sizeof(rom), fp) != sizeof(rom)) {
        printf("FAIL write mbc2 rom file\n");
        test_failures++;
        fclose(fp);
        return;
    }
    fclose(fp);

    gbc = gbc_init();
    if (gbc == NULL) {
        printf("FAIL init mbc2 gbc\n");
        test_failures++;
        return;
    }
    expect_int("mbc2 battery first load", gbc_load_file(gbc, rom_path), GBC_OK);
    gbc_cart_write(gbc, 0x0000, 0x0A);
    gbc_cart_write_ram(gbc, 0xA055, 0x0D);
    expect_int("mbc2 battery first unload", gbc_unload_file(gbc), GBC_OK);
    expect_int("mbc2 battery deinit", gbc_deinit(gbc), GBC_OK);

    gbc = gbc_init();
    if (gbc == NULL) {
        printf("FAIL reinit mbc2 gbc\n");
        test_failures++;
        return;
    }
    expect_int("mbc2 battery second load", gbc_load_file(gbc, rom_path), GBC_OK);
    gbc_cart_write(gbc, 0x0000, 0x0A);
    expect_u8("mbc2 battery restored nibble", gbc_cart_read_ram(gbc, 0xA055), 0xFD);
    expect_int("mbc2 battery second deinit", gbc_deinit(gbc), GBC_OK);

    remove(rom_path);
    remove(save_path);
}

static void expect_color(const char *name, gbc_color_t actual, gbc_color_t expected)
{
    if (actual != expected) {
        printf("FAIL %s: got 0x%08X expected 0x%08X\n", name,
               (unsigned)actual, (unsigned)expected);
        test_failures++;
    }
}

/* Object palette ram holds 15 bit RGB555 colours, two bytes each, red in bits
 * 0..4, green in 5..9, blue in 10..14.  install_cgb_obj_palette therefore takes
 * the 8 bit wide colour the framebuffer uses and stores the matching 5 bit
 * value, so a test can compare a rendered pixel against the same colour. */
static uint16_t rgb555_from_color(gbc_color_t color)
{
    uint8_t r5 = (uint8_t)((((color >> 16) & 0xFF) >> 3) & 0x1F);
    uint8_t g5 = (uint8_t)((((color >> 8) & 0xFF) >> 3) & 0x1F);
    uint8_t b5 = (uint8_t)(((color & 0xFF) >> 3) & 0x1F);

    return (uint16_t)((b5 << 10) | (g5 << 5) | r5);
}

static void install_cgb_obj_palette(gbc_t *gbc, uint8_t palette_id, uint8_t color_id, gbc_color_t color)
{
    /* OBJ has its own 64-byte palette RAM. */
    uint16_t rgb555 = rgb555_from_color(color);
    uint8_t base = (uint8_t)((palette_id * 8U) + (color_id * 2U));

    gbc->ppu.obj_palette[base] = (uint8_t)(rgb555 & 0xFF);
    gbc->ppu.obj_palette[base + 1] = (uint8_t)(rgb555 >> 8);
}

static void test_sprite_selection_keeps_first_ten_oam_entries(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));
    fill_test_rom(rom, 0x00, 0x00);
    gbc.model = GBC_MODEL_DMG;
    gbc.mmu.io[0x40] = 0x82; /* LCD on, BG on, OBJ on */
    gbc.mmu.io[0x47] = 0xE4; /* BG palette: index 0 white */
    gbc.mmu.io[0x48] = 0xE4; /* OBP0: colour 3 black, colour 1 light grey */
    gbc.mmu.vram[0][0] = 0xFF;
    gbc.mmu.vram[0][1] = 0xFF; /* tile 0, colour 3 */
    gbc.mmu.vram[0][16] = 0xFF;
    gbc.mmu.vram[0][17] = 0xFF; /* tile 1, colour 3 */

    for (uint8_t i = 0; i < 40; i++) {
        gbc.mmu.oam[(i * 4) + 0] = 200; /* parked far below scanline 0 */
    }
    /* OAM slots 0..11 are on the scanline, in OAM order their X values
     * descend, so the first ten in OAM order are not the ten leftmost. */
    gbc.mmu.oam[0] = 16;
    gbc.mmu.oam[1] = 30; /* sprite 0: screen X 22 */
    gbc.mmu.oam[2] = 0;
    gbc.mmu.oam[3] = 0x00;
    for (uint8_t i = 1; i <= 11; i++) {
        gbc.mmu.oam[(i * 4) + 0] = 16;
        gbc.mmu.oam[(i * 4) + 1] = (uint8_t)(30 - i); /* screen X 21 down to 11 */
        gbc.mmu.oam[(i * 4) + 2] = 1;
        gbc.mmu.oam[(i * 4) + 3] = 0x00;
    }
    /* Slot 30 is the leftmost sprite of the whole scanline. */
    gbc.mmu.oam[(30 * 4) + 0] = 16;
    gbc.mmu.oam[(30 * 4) + 1] = 9; /* screen X 1 */
    gbc.mmu.oam[(30 * 4) + 2] = 1;
    gbc.mmu.oam[(30 * 4) + 3] = 0x00;

    expect_int("sprite limit rom load", gbc_cart_load_rom(&gbc, rom, sizeof(rom), 0), GBC_OK);
    gbc_ppu_render_scanline(&gbc, 0);

    expect_color("later OAM sprite is excluded", gbc.gbc_draw_data[2], gbc_ppu_color(31, 31, 31));
    expect_color("first ten sprites draw", gbc.gbc_draw_data[25], gbc_ppu_color(0, 0, 0));
    expect_color("background outside sprite span", gbc.gbc_draw_data[31], gbc_ppu_color(31, 31, 31));

    gbc_cart_unload(&gbc);
}

static void test_cgb_sprite_palette_and_priority(void)
{
    gbc_t gbc;
    gbc_color_t dark = gbc_ppu_color(21, 21, 21);
    gbc_color_t light = gbc_ppu_color(10, 10, 10);

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.model = GBC_MODEL_CGB;
    gbc.mmu.io[0x40] = 0x82; /* LCD on, BG on, OBJ on */
    gbc.mmu.vram[0][0] = 0xFF;
    gbc.mmu.vram[0][1] = 0x00; /* tile 0, colour 1 */
    gbc.mmu.vram[0][16] = 0x00;
    gbc.mmu.vram[0][17] = 0xFF; /* tile 1, colour 2 */

    for (uint8_t i = 0; i < 40; i++) {
        gbc.mmu.oam[(i * 4) + 0] = 200;
    }
    for (uint8_t i = 2; i < 12; i++) {
        gbc.mmu.oam[(i * 4) + 0] = 16;
        gbc.mmu.oam[(i * 4) + 1] = 18; /* screen X 10 */
        gbc.mmu.oam[(i * 4) + 2] = 1;
        gbc.mmu.oam[(i * 4) + 3] = 0x01; /* palette 1 */
    }
    gbc.mmu.oam[0] = 16;
    gbc.mmu.oam[1] = 10; /* sprite 0: screen X 2..9 */
    gbc.mmu.oam[2] = 0;
    gbc.mmu.oam[3] = 0x00; /* palette 0 */
    gbc.mmu.oam[4] = 16;
    gbc.mmu.oam[5] = 18; /* sprite 1: screen X 10..17 */
    gbc.mmu.oam[6] = 1;
    gbc.mmu.oam[7] = 0x01; /* palette 1 */

    install_cgb_obj_palette(&gbc, 0, 1, dark);
    install_cgb_obj_palette(&gbc, 1, 2, light);
    expect_u16("obj palette 0 colour 1", (uint16_t)(gbc.ppu.obj_palette[2] | (gbc.ppu.obj_palette[3] << 8)),
               rgb555_from_color(dark));
    expect_u16("obj palette 1 colour 2", (uint16_t)(gbc.ppu.obj_palette[12] | (gbc.ppu.obj_palette[13] << 8)),
               rgb555_from_color(light));

    gbc_ppu_render_scanline(&gbc, 0);

    expect_color("cgb leftmost sprite owns its pixel", gbc.gbc_draw_data[2], dark);
    expect_color("cgb second sprite still drawn", gbc.gbc_draw_data[10], light);
    expect_color("cgb trailing sprites still drawn", gbc.gbc_draw_data[12], light);
    expect_color("cgb background survives sprites", gbc.gbc_draw_data[30], gbc_ppu_color(0, 0, 0));
}

static void test_window_stays_off_when_wx_is_past_screen(void)
{
    gbc_t gbc;
    gbc_color_t white = gbc_ppu_color(31, 31, 31);
    gbc_color_t black = gbc_ppu_color(0, 0, 0);

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.model = GBC_MODEL_DMG;
    /* LCD on, BG on, OBJ off, unsigned tiles, BG map 0x1C00, window map 0x1800. */
    gbc.mmu.io[0x40] = 0xB1;
    gbc.mmu.io[0x47] = 0xE4; /* BG palette: colour 0 white, colour 3 black */
    gbc.mmu.io[0x4A] = 0;    /* WY */
    gbc.mmu.io[0x4B] = 180;  /* WX past the visible area */
    gbc.mmu.vram[0][0x0000] = 0x00;
    gbc.mmu.vram[0][0x0001] = 0x00; /* tile 0: colour 0 white */
    gbc.mmu.vram[0][0x0010] = 0xFF;
    gbc.mmu.vram[0][0x0011] = 0xFF; /* tile 1: colour 3 black */
    /* The background map points at the white tile, the window map at the black
     * one, so the window is visible in the picture whenever it is used. */
    gbc.mmu.vram[0][0x1C00] = 0x00;
    gbc.mmu.vram[0][0x1800] = 0x01;

    gbc_ppu_render_scanline(&gbc, 0);
    expect_color("window off when wx over 166", gbc.gbc_draw_data[159], white);

    /* WX 166 is the last value that still places the window on screen: its
     * left edge lands at screen X 159, which is the black tile.  Anything
     * above 166 leaves the window off for the whole line, so the pixel stays
     * white rather than being clamped to the right edge. */
    gbc.ppu.win_line = 0;
    gbc.mmu.io[0x4B] = 166;
    gbc_ppu_render_scanline(&gbc, 0);
    expect_color("window on at wx 166", gbc.gbc_draw_data[159], black);
}

static void test_ppu_mode3_extends_with_scx_and_visible_sprites(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.model = GBC_MODEL_DMG;
    gbc.mmu.io[0x40] = 0x82;
    gbc.mmu.io[0x41] = 0x83;
    gbc.mmu.io[0x43] = 0x07;
    gbc.ppu.mode = 3;
    gbc.ppu.dot_cycles = 251;
    gbc.mmu.oam[0] = 16;
    gbc.mmu.oam[1] = 16;

    gbc_ppu_step(&gbc, 1);
    expect_u8("ppu mode3 still active with penalties", gbc.mmu.io[0x41] & 0x03, 0x03);

    gbc_ppu_step(&gbc, 13);
    expect_u8("ppu mode3 eventually enters hblank", gbc.mmu.io[0x41] & 0x03, 0x00);
}

static void test_stat_lyc_updates_on_lyc_write(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.mmu.io[0x41] = 0xC0;
    gbc.mmu.io[0x44] = 7;
    gbc.mmu.io[0x0F] = 0xE0;

    gbc_mmu_write(&gbc, 0xFF45, 7);

    expect_u8("stat lyc coincidence flag on write", gbc.mmu.io[0x41] & 0x04, 0x04);
    expect_u8("stat lyc interrupt on write", gbc.mmu.io[0x0F] & GBC_IF_LCD, GBC_IF_LCD);
}

static void test_serial_internal_clock_transfer_completes(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.mmu.io[0x0F] = 0xE0;

    gbc_mmu_write(&gbc, 0xFF01, 0xA5);
    gbc_mmu_write(&gbc, 0xFF02, 0x81);
    expect_u8("serial transfer starts", gbc_mmu_read(&gbc, 0xFF02) & 0x80, 0x80);

    gbc_mmu_step_serial(&gbc, 4095);
    expect_u8("serial transfer not early", gbc_mmu_read(&gbc, 0xFF02) & 0x80, 0x80);
    expect_u8("serial interrupt not early", gbc.mmu.io[0x0F] & GBC_IF_SERIAL, 0x00);

    gbc_mmu_step_serial(&gbc, 1);
    expect_u8("serial transfer clears start", gbc_mmu_read(&gbc, 0xFF02) & 0x80, 0x00);
    expect_u8("serial fills incoming byte", gbc_mmu_read(&gbc, 0xFF01), 0xFF);
    expect_u8("serial requests interrupt", gbc.mmu.io[0x0F] & GBC_IF_SERIAL, GBC_IF_SERIAL);
}

static void test_ei_waits_for_next_instruction(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];
    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));
    gbc.cart.rom_data = rom;
    gbc.cpu.pc = 0x100;
    gbc.cpu.sp = 0xFFFE;
    gbc.mmu.ie = GBC_IF_VBLANK;
    gbc.mmu.io[0x0F] = GBC_IF_VBLANK;
    rom[0x100] = 0xFB;
    rom[0x101] = 0x3C;
    expect_u8("EI executes", gbc_cpu_step(&gbc), 4);
    expect_u8("EI stays disabled", gbc.cpu.ime, 0);
    expect_u8("instruction after EI executes before IRQ", gbc_cpu_step(&gbc), 4);
    expect_u8("INC A executes", gbc.cpu.af.hi, 1);
    expect_u8("IRQ after following instruction", gbc_cpu_step(&gbc), 20);
    gbc.cpu.pc = 0x100;
    rom[0x101] = 0xF3;
    gbc.mmu.io[0x0F] = GBC_IF_VBLANK;
    gbc_cpu_step(&gbc);
    gbc_cpu_step(&gbc);
    expect_u8("DI cancels pending EI", gbc.cpu.ime, 0);
    expect_u8("DI clears delay", gbc.cpu.ime_delay, 0);
    gbc.cpu.pc = 0x100;
    rom[0x101] = 0xFB;
    gbc_cpu_step(&gbc);
    gbc_cpu_step(&gbc);
    expect_u8("second EI does not extend delay", gbc.cpu.ime, 1);
}

static void test_cgb_obj_palette7_and_oam_priority(void)
{
    gbc_t gbc;
    gbc_color_t dark = gbc_ppu_color(10, 10, 10);
    gbc_color_t light = gbc_ppu_color(20, 20, 20);
    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.model = GBC_MODEL_CGB;
    gbc.mmu.io[0x40] = 0x82;
    gbc.mmu.vram[0][0] = 0xFF;
    gbc.mmu.oam[0] = gbc.mmu.oam[4] = 16;
    gbc.mmu.oam[1] = 18;
    gbc.mmu.oam[3] = 7;
    gbc.mmu.oam[5] = 16;
    install_cgb_obj_palette(&gbc, 7, 1, dark);
    install_cgb_obj_palette(&gbc, 0, 1, light);
    gbc_ppu_render_scanline(&gbc, 0);
    expect_color("CGB lower OAM wins despite larger X", gbc.ppu.line[10], dark);
    expect_color("CGB other sprite visible", gbc.ppu.line[8], light);
}

static void test_double_speed_clocks_and_dma(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];
    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc_memset(rom, 0, sizeof(rom));
    gbc.cart.rom_data = rom;
    gbc.model = GBC_MODEL_CGB;
    gbc.mmu.double_speed = 1;
    gbc.mmu.io[0x40] = 0x80;
    expect_u8("fast CPU NOP", gbc_step(&gbc), 4);
    expect_u16("timer follows CPU clock", gbc.timer.div_counter, 4);
    expect_u16("PPU follows dot clock", gbc.ppu.dot_cycles, 2);
    gbc_mmu_write(&gbc, 0xFF55, 0);
    gbc.mmu.ie = gbc.mmu.io[0x0F] = GBC_IF_VBLANK;
    gbc.cpu.ime = 1;
    expect_u8("DMA stalls even pending interrupt", gbc_cpu_step(&gbc), 4);
    expect_u8("DMA keeps IME", gbc.cpu.ime, 1);
    gbc_mmu_step_dma(&gbc, 63);
    expect_u8("fast DMA waits 64 CPU cycles", gbc_mmu_cpu_stalled(&gbc), 1);
    gbc_mmu_step_dma(&gbc, 1);
    expect_u8("fast DMA finishes", gbc_mmu_cpu_stalled(&gbc), 0);
}

static void test_failed_load_retains_caller_rom(void)
{
    gbc_t gbc;
    static uint8_t rom[32768];
    int before = test_frees;
    gbc_memset(&gbc, 0, sizeof(gbc));
    fill_test_rom(rom, 0x03, 0x02);
    test_fail_allocation = 1;
    expect_int("failed cart RAM allocation", gbc_cart_load_rom(&gbc, rom, sizeof(rom), 1), GBC_ERROR_ALLOC);
    test_fail_allocation = 0;
    expect_int("failed load did not free caller ROM", test_frees, before);
    expect_u8("failed load clears ownership", gbc.cart.owns_rom, 0);
}

static void test_jr_fetch_order(void)
{
    gbc_t gbc;
    const int offsets[] = {-128, -2, -1, 0, 1, 127};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        gbc_init_static(&gbc); gbc.cpu.pc = 0xC100;
        gbc_mmu_write(&gbc, 0xC100, 0x18);
        gbc_mmu_write(&gbc, 0xC101, (uint8_t)offsets[i]);
        expect_u8("JR cycles", gbc_cpu_step(&gbc), 12);
        expect_u16("JR post-fetch PC", gbc.cpu.pc, (uint16_t)(0xC102 + offsets[i]));
        gbc_deinit_static(&gbc);
    }
}

static void test_timer_zero_cycles_is_noop(void)
{
    gbc_t gbc;
    gbc_init_static(&gbc); gbc.timer.div_counter = 0x1234;
    gbc.timer.tima_reload_delay = 3; gbc.mmu.io[4] = 0x9A;
    gbc_timer_step(&gbc, 0);
    expect_u8("zero clocks preserve DIV mirror", gbc.mmu.io[4], 0x9A);
    expect_u16("zero clocks preserve divider", gbc.timer.div_counter, 0x1234);
    expect_u8("zero clocks preserve reload", gbc.timer.tima_reload_delay, 3);
    gbc_deinit_static(&gbc);
}

int main(void)
{
    test_timer_zero_cycles_is_noop();
    test_jr_fetch_order();
    test_failed_load_retains_caller_rom();
    test_ei_waits_for_next_instruction();
    test_cgb_obj_palette7_and_oam_priority();
    test_double_speed_clocks_and_dma();
    test_halt_bug_reuses_next_opcode_byte();
    test_halt_keeps_cpu_stopped_while_ime_clear();
    test_halt_wakes_on_pending_irq_while_ime_is_off();
    test_halt_bug_without_ime_runs_next_instruction();
    test_mbc3_rtc_registers_count_and_latch();
    test_battery_ram_survives_reload();
    test_timer_div_write_falling_edge_increments_tima();
    test_timer_tac_write_falling_edge_increments_tima();
    test_timer_overflow_delays_reload_and_interrupt();
    test_dmg_sprite_lower_x_has_priority_over_oam_order();
    test_hblank_hdma_copies_one_block_per_hblank();
    test_general_purpose_hdma_stalls_cpu_until_finished();
    test_general_purpose_hdma_length_scales_with_blocks();
    test_oam_dma_blocks_non_hram_until_transfer_finishes();
    test_mbc2_loads_banks_and_internal_4bit_ram();
    test_largest_rom_size_code_reports_512_banks();
    test_mbc2_battery_ram_survives_reload();
    test_sprite_selection_keeps_first_ten_oam_entries();
    test_cgb_sprite_palette_and_priority();
    test_window_stays_off_when_wx_is_past_screen();
    test_ppu_mode3_extends_with_scx_and_visible_sprites();
    test_stat_lyc_updates_on_lyc_write();
    test_serial_internal_clock_transfer_completes();

    if (test_failures != 0) {
        printf("%d regression test(s) failed\n", test_failures);
        return 1;
    }
    printf("core regression tests passed\n");
    return 0;
}
