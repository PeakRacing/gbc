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

static int test_failures;
static uint8_t last_sound_buffer[128];
static size_t last_sound_len;

void *gbc_malloc(int num) { return malloc((size_t)num); }
void gbc_free(void *address) { free(address); }
void *gbc_memcpy(void *str1, const void *str2, size_t n) { return memcpy(str1, str2, n); }
void *gbc_memset(void *str, int c, size_t n) { return memset(str, c, n); }
int gbc_memcmp(const void *str1, const void *str2, size_t n) { return memcmp(str1, str2, n); }
FILE *gbc_fopen(const char *filename, const char *mode) { return fopen(filename, mode); }
size_t gbc_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) { return fread(ptr, size, nmemb, stream); }
size_t gbc_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) { return fwrite(ptr, size, nmemb, stream); }
int gbc_fseek(FILE *stream, long int offset, int whence) { return fseek(stream, offset, whence); }
long gbc_ftell(FILE *stream) { return ftell(stream); }
int gbc_fclose(FILE *stream) { return fclose(stream); }
int gbc_draw(int x1, int y1, int x2, int y2, gbc_color_t *color_data)
{
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color_data;
    return GBC_OK;
}

int gbc_sound_output(uint8_t *buffer, size_t len)
{
    if (len > sizeof(last_sound_buffer)) {
        len = sizeof(last_sound_buffer);
    }
    gbc_memcpy(last_sound_buffer, buffer, len);
    last_sound_len = len;
    return GBC_OK;
}

int gbc_log_printf(const char *format, ...)
{
    (void)format;
    return 0;
}

uint32_t gbc_get_ticks(void) { return 0; }
void gbc_delay(uint32_t ms) { (void)ms; }
int gbc_initex(gbc_t *gbc) { (void)gbc; return GBC_OK; }
int gbc_deinitex(gbc_t *gbc) { (void)gbc; return GBC_OK; }
void gbc_frame(gbc_t *gbc) { (void)gbc; }

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

static int16_t read_s16le(uint8_t *buffer)
{
    return (int16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8));
}

static void test_ch1_clearing_negate_after_negative_sweep_disables_channel(void)
{
    gbc_t gbc;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.mmu.io[0x26] = 0x80;
    gbc_apu_reset(&gbc);
    gbc_apu_write(&gbc, 0x10, 0x19);
    gbc_apu_write(&gbc, 0x11, 0x80);
    gbc_apu_write(&gbc, 0x12, 0xF0);
    gbc_apu_write(&gbc, 0x13, 0xE8);
    gbc_apu_write(&gbc, 0x14, 0x83);

    gbc.apu.frame_step = 2;
    gbc.apu.frame_acc = GBC_CPU_CLOCK_FREQ - 512;
    gbc.apu.square1.sweep_timer = 1;
    gbc_apu_step(&gbc, 1);
    expect_u8("apu negative sweep keeps ch1 enabled", gbc.apu.square1.enabled, 1);

    gbc_apu_write(&gbc, 0x10, 0x11);
    expect_u8("apu clearing used negate disables ch1", gbc.apu.square1.enabled, 0);
}

static void test_apu_outputs_interleaved_stereo_samples(void)
{
    gbc_t gbc;
    int16_t left;
    int16_t right;

    gbc_memset(&gbc, 0, sizeof(gbc));
    gbc.mmu.io[0x26] = 0x80;
    gbc_apu_reset(&gbc);
    gbc.apu.square1.enabled = 1;
    gbc.apu.square1.dac_enabled = 1;
    gbc.apu.square1.duty = 0;
    gbc.apu.square1.duty_step = 7;
    gbc.apu.square1.envelope_volume = 4;
    gbc.mmu.io[0x24] = 0x77;
    gbc.mmu.io[0x25] = 0x10;
    gbc.apu.sample_acc = GBC_CPU_CLOCK_FREQ - GBC_APU_SAMPLE_RATE;

    gbc_apu_step(&gbc, 1);

    expect_u16("apu stereo sample bytes", gbc.apu.sample_index, 4);
    left = read_s16le(&gbc.apu.sample_buffer[0]);
    right = read_s16le(&gbc.apu.sample_buffer[2]);
    if (left == 0) {
        printf("FAIL apu stereo left sample: got %d expected non-zero\n", left);
        test_failures++;
    }
    if (right != 0) {
        printf("FAIL apu stereo right sample: got %d expected 0\n", right);
        test_failures++;
    }
    (void)last_sound_len;
}

int main(void)
{
    test_ch1_clearing_negate_after_negative_sweep_disables_channel();
    test_apu_outputs_interleaved_stereo_samples();

    if (test_failures != 0) {
        printf("%d APU regression test(s) failed\n", test_failures);
        return 1;
    }
    printf("APU regression tests passed\n");
    return 0;
}
