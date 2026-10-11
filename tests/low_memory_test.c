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
static int test_allocations;
static int test_draws;

void *gbc_malloc(int num)
{
    test_allocations++;
    return malloc((size_t)num);
}

void gbc_free(void *address)
{
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

#if (GBC_USE_FS == 1)
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

#endif

int gbc_draw(int x1, int y1, int x2, int y2, gbc_color_t *color_data)
{
    if (x1 != 0 || x2 != GBC_WIDTH - 1 || y1 != y2 ||
        y1 != test_draws || color_data == NULL) {
        test_failures++;
    }
    test_draws++;
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

int main(void)
{
    static gbc_t gbc;
    static uint8_t rom[32768];
    int before;
    if (gbc_init_static(&gbc) != GBC_OK) {
        return 1;
    }
    rom[0x147] = 0x03; /* MBC1 + battery RAM */
    rom[0x149] = 0x03; /* 32 KiB, disabled in this build */
    before = test_allocations;
    if (gbc_load_rom(&gbc, rom, sizeof(rom)) != GBC_OK ||
        gbc.cart.rom_data != rom || gbc.cart.owns_rom ||
        gbc.cart.ram_data != NULL || test_allocations != before) {
        test_failures++;
    }
    gbc_mmu_write(&gbc, 0xFF70, 7);
    gbc_mmu_write(&gbc, 0xFF4F, 1);
    gbc_mmu_write(&gbc, 0xD000, 0x5A);
    if (gbc.mmu.wram_bank != 1 || gbc.mmu.vram_bank != 0 ||
        gbc_mmu_read(&gbc, 0xD000) != 0x5A) {
        test_failures++;
    }
    for (uint8_t ly = 0; ly < GBC_HEIGHT; ly++) {
        gbc_ppu_render_scanline(&gbc, ly);
    }
    if (test_draws != GBC_HEIGHT || test_allocations != before ||
        sizeof(gbc.mmu.vram) != 8192 || sizeof(gbc.mmu.wram) != 8192) {
        test_failures++;
    }
    if (gbc_deinit_static(&gbc) != GBC_OK || gbc.cart.rom_data != NULL) {
        test_failures++;
    }
    printf("sizeof(gbc_t)=%zu, WRAM=%zu, VRAM=%zu, line=%zu, allocations=%d, draws=%d\n",
           sizeof(gbc), sizeof(gbc.mmu.wram), sizeof(gbc.mmu.vram),
           sizeof(gbc.ppu.line), test_allocations, test_draws);
    printf("low-memory tests: %s\n", test_failures ? "FAILED" : "passed");
    return test_failures ? 1 : 0;
}
