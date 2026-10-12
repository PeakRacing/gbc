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
static int feature_fault_write, feature_fault_sync, feature_fault_replace, feature_fault_close;
static int feature_fault_read, feature_read_count;
static unsigned feature_allocations;
static uint32_t feature_audio_hash;
static int test_fail_allocation;
static int test_frees;

void *gbc_malloc(int num)
{
    feature_allocations++;
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
    feature_read_count++;
    if (feature_fault_read && feature_read_count >= feature_fault_read) {
        return 0;
    }
    return fread(ptr, size, nmemb, stream);
}

size_t gbc_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return feature_fault_write ? 0 : fwrite(ptr, size, nmemb, stream);
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
    int ret = fclose(stream);
    return feature_fault_close ? -1 : ret;
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
    for (size_t i = 0; i < len; i++) { feature_audio_hash = (feature_audio_hash ^ buffer[i]) * 16777619UL; }
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


#define gbc_file_sync feature_platform_sync
#define gbc_file_replace feature_platform_replace
#define gbc_file_remove feature_platform_remove
#define gbc_file_exists feature_platform_exists
#include "../port/gbc_fs_impl.h"
#undef gbc_file_sync
#undef gbc_file_replace
#undef gbc_file_remove
#undef gbc_file_exists
int gbc_file_sync(FILE *fp) { return feature_fault_sync ? -1 : feature_platform_sync(fp); }
int gbc_file_replace(const char *a, const char *b) { return feature_fault_replace ? -1 : feature_platform_replace(a, b); }
int gbc_file_remove(const char *p) { return feature_platform_remove(p); }
int gbc_file_exists(const char *p) { return feature_platform_exists(p); }

void gbc_state_loaded(gbc_t *gbc) { (void)gbc; }

static gbc_t feature_gbc;
static uint8_t feature_rom[32768];

#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); test_failures++; } } while (0)

static void feature_setup(uint8_t type)
{
    gbc_unload_rom_discard(&feature_gbc);
    memset(feature_rom, 0, sizeof(feature_rom));
    feature_rom[0x147] = type;
    feature_rom[0x149] = type == 0x06 ? 0 : 2;
    CHECK(gbc_init_static(&feature_gbc) == GBC_OK);
    CHECK(gbc_load_rom(&feature_gbc, feature_rom, sizeof(feature_rom)) == GBC_OK);
    CHECK(gbc_persist_set_path(&feature_gbc, "feature_test") == GBC_OK);
    gbc_cart_write(&feature_gbc, 0, 0x0A);
}

static void feature_battery(void)
{
    remove("feature_test.sav");
    feature_setup(0x03);
    CHECK(gbc_persist_load(&feature_gbc) == GBC_OK);
    CHECK(feature_gbc.cart.ram_dirty == 0);
    gbc_cart_write_ram(&feature_gbc, 0xA123, 0x5A);
    unsigned allocations = feature_allocations;
    CHECK(gbc_persist_save(&feature_gbc) == GBC_OK);
    CHECK(feature_allocations == allocations);
    CHECK(feature_gbc.cart.ram_dirty == 0);
    gbc_cart_write_ram(&feature_gbc, 0xA123, 0x5A);
    CHECK(feature_gbc.cart.ram_dirty == 0);
    gbc_cart_write_ram(&feature_gbc, 0xA123, 0x6B);
    for (int fault = 0; fault < 4; fault++) {
        feature_fault_write = fault == 0;
        feature_fault_sync = fault == 1;
        feature_fault_replace = fault == 2;
        feature_fault_close = fault == 3;
        CHECK(gbc_unload_rom(&feature_gbc) == GBC_ERROR_IO);
        CHECK(feature_gbc.cart.rom_data == feature_rom);
        CHECK(feature_gbc.cart.ram_dirty == 1);
        FILE *fp = fopen("feature_test.sav", "rb");
        CHECK(fp != NULL);
        if (fp) {
            fseek(fp, 0x123, SEEK_SET);
            CHECK(fgetc(fp) == 0x5A);
            fclose(fp);
        }
    }
    feature_fault_write = feature_fault_sync = feature_fault_replace = feature_fault_close = 0;
    CHECK(gbc_unload_rom(&feature_gbc) == GBC_OK);
    feature_setup(0x03);
    CHECK(gbc_persist_load(&feature_gbc) == GBC_OK);
    CHECK(gbc_cart_read_ram(&feature_gbc, 0xA123) == 0x6B);
    feature_fault_read = 1; feature_read_count = 0;
    CHECK(gbc_persist_load(&feature_gbc) == GBC_ERROR_IO);
    CHECK(feature_gbc.cart.persist_blocked == 1);
    feature_fault_read = 0;
    CHECK(gbc_persist_load(&feature_gbc) == GBC_OK);
    FILE *fp = fopen("feature_test.sav", "wb");
    CHECK(fp != NULL);
    if (fp) { fputc(0x42, fp); fclose(fp); }
    CHECK(gbc_persist_load(&feature_gbc) == GBC_ERROR_FORMAT);
    CHECK(gbc_persist_save(&feature_gbc) == GBC_ERROR_FORMAT);
    CHECK(gbc_unload_rom_discard(&feature_gbc) == GBC_OK);
    remove("feature_test.sav");
    feature_setup(0x06);
    gbc_cart_write_ram(&feature_gbc, 0xA055, 0xFD);
    CHECK(gbc_persist_save(&feature_gbc) == GBC_OK);
    feature_setup(0x06);
    CHECK(gbc_persist_load(&feature_gbc) == GBC_OK);
    CHECK(gbc_cart_read_ram(&feature_gbc, 0xA055) == 0xFD);
    CHECK(gbc_deinit_static(&feature_gbc) == GBC_OK);
    remove("feature_test.sav");
}


static void feature_rtc(void)
{
    remove("feature_test.rtc");
    feature_setup(0x10);
    test_ticks = 100;
    feature_gbc.cart.rtc_last_ticks = test_ticks;
    feature_gbc.cart.rtc_regs[0] = 59;
    feature_gbc.cart.rtc_regs[1] = 59;
    feature_gbc.cart.rtc_regs[2] = 23;
    feature_gbc.cart.rtc_regs[3] = 0xFF;
    feature_gbc.cart.rtc_regs[4] = 1;
    test_ticks += 1500;
    CHECK(gbc_persist_save(&feature_gbc) == GBC_OK);
    CHECK(feature_gbc.cart.rtc_regs[0] == 0);
    CHECK(feature_gbc.cart.rtc_regs[3] == 0);
    CHECK(feature_gbc.cart.rtc_regs[4] == 0x80);
    CHECK(feature_gbc.cart.rtc_ms == 500);
    CHECK(gbc_unload_rom_discard(&feature_gbc) == GBC_OK);
    test_ticks += 1000000;
    feature_setup(0x10);
    CHECK(gbc_persist_load(&feature_gbc) == GBC_OK);
    CHECK(feature_gbc.cart.rtc_regs[0] == 0);
    CHECK(feature_gbc.cart.rtc_ms == 500);
    test_ticks += 500;
    gbc_cart_rtc_sync(&feature_gbc);
    CHECK(feature_gbc.cart.rtc_regs[0] == 1);
    feature_gbc.cart.rtc_regs[4] = 0xC0;
    feature_gbc.cart.rtc_latched[0] = 42;
    feature_gbc.cart.rtc_latch_prev = 1;
    CHECK(gbc_persist_save(&feature_gbc) == GBC_OK);
    gbc_unload_rom_discard(&feature_gbc);
    test_ticks += 5000;
    feature_setup(0x10);
    CHECK(gbc_persist_load(&feature_gbc) == GBC_OK);
    CHECK(feature_gbc.cart.rtc_latched[0] == 42);
    CHECK(feature_gbc.cart.rtc_latch_prev == 1);
    test_ticks += 5000;
    gbc_cart_rtc_sync(&feature_gbc);
    CHECK(feature_gbc.cart.rtc_regs[0] == 1);
    CHECK(feature_gbc.cart.rtc_regs[4] == 0xC0);
    feature_fault_replace = 1;
    CHECK(gbc_persist_save(&feature_gbc) == GBC_ERROR_IO);
    feature_fault_replace = 0;
    FILE *fp = fopen("feature_test.rtc", "r+b");
    CHECK(fp != NULL);
    if (fp) { fputc('X', fp); fclose(fp); }
    CHECK(gbc_persist_load(&feature_gbc) == GBC_ERROR_FORMAT);
    CHECK(feature_gbc.cart.rtc_regs[0] == 0);
    CHECK(gbc_persist_save(&feature_gbc) == GBC_ERROR_FORMAT);
    gbc_unload_rom_discard(&feature_gbc);
    remove("feature_test.rtc");
    remove("feature_test.sav");
}


static uint32_t feature_hash(const void *data, size_t size)
{
    const uint8_t *p = data;
    uint32_t h = 2166136261UL;
    while (size--) { h = (h ^ *p++) * 16777619UL; }
    return h;
}

static void feature_run_frames(uint32_t n)
{
    uint32_t target = feature_gbc.frame_count + n;
    unsigned steps = 0;
    while (feature_gbc.frame_count < target && steps++ < 2000000) { gbc_step(&feature_gbc); }
    CHECK(feature_gbc.frame_count == target);
}

static void feature_state(void)
{
    feature_setup(0x03);
    remove("feature_test.state");
    /* Keep execution in an intentionally authored NOP/JP loop. */
    feature_rom[0x100] = 0;
    feature_rom[0x101] = 0xC3;
    feature_rom[0x102] = 0;
    feature_rom[0x103] = 1;
    feature_run_frames(2);
    unsigned allocations = feature_allocations;
    CHECK(gbc_state_save_file(&feature_gbc, "feature_test.state") == GBC_OK);
    feature_audio_hash = 2166136261UL;
    feature_run_frames(3);
    uint32_t audio = feature_audio_hash;
    uint32_t cpu = feature_hash(&feature_gbc.cpu, sizeof(feature_gbc.cpu));
    uint32_t mmu = feature_hash(&feature_gbc.mmu, sizeof(feature_gbc.mmu));
    uint32_t pixels = feature_hash(feature_gbc.gbc_draw_data, sizeof(feature_gbc.gbc_draw_data));
    feature_read_count = 0;
    CHECK(gbc_state_load_file(&feature_gbc, "feature_test.state") == GBC_OK);
    int load_reads = feature_read_count;
    feature_audio_hash = 2166136261UL;
    feature_run_frames(3);
    CHECK(feature_audio_hash == audio);
    CHECK(cpu == feature_hash(&feature_gbc.cpu, sizeof(feature_gbc.cpu)));
    CHECK(mmu == feature_hash(&feature_gbc.mmu, sizeof(feature_gbc.mmu)));
    CHECK(pixels == feature_hash(feature_gbc.gbc_draw_data, sizeof(feature_gbc.gbc_draw_data)));
    CHECK(feature_allocations == allocations);
    uint16_t pc = feature_gbc.cpu.pc;
    FILE *fp = fopen("feature_test.state", "r+b");
    CHECK(fp != NULL);
    if (fp) { fseek(fp, 8, SEEK_SET); int b = fgetc(fp); fseek(fp, 8, SEEK_SET); fputc(b ^ 1, fp); fclose(fp); }
    CHECK(gbc_state_load_file(&feature_gbc, "feature_test.state") == GBC_ERROR_MISMATCH);
    CHECK(feature_gbc.cpu.pc == pc);
    CHECK(gbc_state_save_file(&feature_gbc, "feature_test.state") == GBC_OK);
    fp = fopen("feature_test.state", "r+b");
    CHECK(fp != NULL);
    if (fp) { fseek(fp, 12, SEEK_SET); int b = fgetc(fp); fseek(fp, 12, SEEK_SET); fputc(b ^ 1, fp); fclose(fp); }
    CHECK(gbc_state_load_file(&feature_gbc, "feature_test.state") == GBC_ERROR_MISMATCH);
    CHECK(feature_gbc.cpu.pc == pc);
    CHECK(gbc_state_save_file(&feature_gbc, "feature_test.state") == GBC_OK);
    fp = fopen("feature_test.state", "r+b");
    CHECK(fp != NULL);
    if (fp) { fseek(fp, 500, SEEK_SET); int b = fgetc(fp); fseek(fp, 500, SEEK_SET); fputc(b ^ 1, fp); fclose(fp); }
    CHECK(gbc_state_load_file(&feature_gbc, "feature_test.state") == GBC_ERROR_FORMAT);
    CHECK(feature_gbc.cpu.pc == pc);
    CHECK(gbc_state_save_file(&feature_gbc, "feature_test.state") == GBC_OK);
    feature_fault_write = 1;
    CHECK(gbc_state_save_file(&feature_gbc, "feature_test.state") == GBC_ERROR_IO);
    feature_fault_write = 0;
    CHECK(gbc_state_load_file(&feature_gbc, "feature_test.state") == GBC_OK);
    feature_read_count = 0;
    feature_fault_read = (load_reads - 1) / 2 + 2;
    CHECK(gbc_state_load_file(&feature_gbc, "feature_test.state") == GBC_ERROR_IO);
    CHECK(feature_gbc.gbc_quit == 1);
    feature_fault_read = 0;
    gbc_unload_rom_discard(&feature_gbc);
    remove("feature_test.state");
    remove("feature_test.sav");
}

int main(void)
{
    feature_battery();
    feature_rtc();
    feature_state();
    printf("feature regression tests: %s\n", test_failures ? "FAILED" : "passed");
    return test_failures ? 1 : 0;
}
