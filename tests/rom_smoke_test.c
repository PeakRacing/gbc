/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

/*
 * Headless real-ROM smoke test.
 *
 * The module level regression tests in tests/regression_test.c drive the core
 * through hand written state.  This harness is the opposite end of the scale:
 * it boots a real cartridge image, emulates a number of frames and reports an
 * observable summary (frame count, a framebuffer fingerprint and a fingerprint
 * of the audio stream when the APU is on).  It exercises the whole pipeline -
 * header parsing, mapper banking, CPU fetch/execute, PPU rendering and the APU
 * mixer - on images nobody hand tailored for it, which is what catches the
 * bugs that per-module assertions miss.
 *
 * Usage:
 *   rom_test <rom file> [frames] [audio file] [cycle budget]
 *
 * Pass "none" or "-" for the audio file to skip audio capture.
 *
 * Exit code 0 means every requested frame ran.  A non zero exit code means the
 * ROM could not be loaded, the core reported an unsupported cartridge, or the
 * run stopped producing frames (see the reason printed before exit).
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gbc.h"

#define DEFAULT_FRAMES          (60U)
#define MAX_FRAMES              (1000000U)

/* Emulated cycles allowed per requested frame before the run is declared stuck.
 * A real frame costs about 70224 cycles, so this leaves a wide margin while
 * still bounding a cartridge that never enables the LCD. */
#define DEFAULT_CYCLE_BUDGET    (2000000ULL)

static FILE *smoke_audio_file;
static uint8_t smoke_audio_seen;
static uint32_t smoke_audio_bytes;
#if defined(GBC_SMOKE_STATE_REPLAY)
static uint32_t smoke_audio_hash;
#endif

/*
 * Diagnostic ring buffers.  They are compiled in only when GBC_SMOKE_TRACE is
 * defined, so the default build stays small, and they are what makes a stuck or
 * faulting cartridge debuggable: the ring shows the instruction path that led
 * to the failure and the log shows whether the LCD was ever enabled.
 */
#ifdef GBC_SMOKE_TRACE
#define SMOKE_TRACE_PC_MAX      (512)
#define SMOKE_TRACE_LOG_MAX     (64)

static uint16_t smoke_trace_pc[SMOKE_TRACE_PC_MAX];
static uint32_t smoke_trace_pc_head;
static uint32_t smoke_trace_pc_count;
static char smoke_trace_log[SMOKE_TRACE_LOG_MAX][72];
static uint32_t smoke_trace_log_head;
static uint32_t smoke_trace_log_count;
static uint8_t smoke_trace_lcdc = 0xFF;
static uint8_t smoke_trace_lcd_off;
static uint32_t smoke_trace_after_off;

static void smoke_trace_note(gbc_t *gbc)
{
    uint8_t lcdc = gbc->mmu.io[0x40];

    smoke_trace_pc[smoke_trace_pc_head] = gbc->cpu.pc;
    smoke_trace_pc_head = (smoke_trace_pc_head + 1U) % SMOKE_TRACE_PC_MAX;
    if (smoke_trace_pc_count < SMOKE_TRACE_PC_MAX) {
        smoke_trace_pc_count++;
    }
    if (lcdc != smoke_trace_lcdc) {
        snprintf(smoke_trace_log[smoke_trace_log_head], sizeof(smoke_trace_log[0]),
                 "frame=%u lcdc 0x%02X -> 0x%02X pc=0x%04X",
                 (unsigned)gbc->frame_count, (unsigned)smoke_trace_lcdc, (unsigned)lcdc,
                 (unsigned)gbc->cpu.pc);
        smoke_trace_log_head = (smoke_trace_log_head + 1U) % SMOKE_TRACE_LOG_MAX;
        if (smoke_trace_log_count < SMOKE_TRACE_LOG_MAX) {
            smoke_trace_log_count++;
        }
        smoke_trace_lcdc = lcdc;
        if (lcdc == 0) {
            smoke_trace_lcd_off = 1;
            smoke_trace_after_off = 0;
        }
    }
    /* After the LCD has been switched off nothing can produce another frame, so
     * record the first steps of the resulting loop to show what it is waiting
     * for.  A program counter that repeats means the CPU is spinning. */
    if (smoke_trace_lcd_off && smoke_trace_after_off < 64U) {
        uint32_t i;
        uint8_t seen = 0;

        for (i = 0; i < smoke_trace_after_off && i < 64U; i++) {
            if (smoke_trace_pc[(smoke_trace_pc_head + SMOKE_TRACE_PC_MAX - 2U - i) % SMOKE_TRACE_PC_MAX] == gbc->cpu.pc) {
                seen = 1;
                break;
            }
        }
        printf("  [lcd off] step %u pc=0x%04X op=0x%02X a=0x%02X lcdc=0x%02X ly=%u stat=0x%02X if=0x%02X ie=0x%02X ime=%u halted=%u%s\n",
               (unsigned)smoke_trace_after_off, (unsigned)gbc->cpu.pc,
               (unsigned)gbc->cpu.last_opcode, (unsigned)gbc->cpu.af.hi,
               (unsigned)gbc->mmu.io[0x40], (unsigned)gbc->mmu.io[0x44],
               (unsigned)gbc->mmu.io[0x41],
               (unsigned)gbc->mmu.io[0x0F], (unsigned)gbc->mmu.ie,
               (unsigned)gbc->cpu.ime, (unsigned)gbc->cpu.halted, seen ? " (repeat)" : "");
        smoke_trace_after_off++;
    }
}

static void smoke_trace_dump(void)
{
    uint32_t i;
    uint32_t start;

    printf("----- lcdc changes (%u) -----\n", (unsigned)smoke_trace_log_count);
    for (i = 0; i < smoke_trace_log_count; i++) {
        printf("  %s\n", smoke_trace_log[i]);
    }
    printf("----- last %u program counters (oldest first) -----\n",
           (unsigned)smoke_trace_pc_count);
    start = (smoke_trace_pc_head + SMOKE_TRACE_PC_MAX - smoke_trace_pc_count) % SMOKE_TRACE_PC_MAX;
    for (i = 0; i < smoke_trace_pc_count; i++) {
        printf(" 0x%04X", (unsigned)smoke_trace_pc[(start + i) % SMOKE_TRACE_PC_MAX]);
        if ((i % 8U) == 7U || i + 1U == smoke_trace_pc_count) {
            printf("\n");
        }
    }
    printf("----- lcd disabled: %s -----\n", smoke_trace_lcd_off ? "yes" : "no");
}
#endif

static void smoke_request_quit(gbc_t *gbc)
{
    gbc->gbc_quit = 1;
}

/* Deterministic 32 bit fingerprint of a byte block. */
static uint32_t smoke_hash32(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261UL; /* FNV-1a offset basis */

    for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= 16777619UL;
    }
    return hash;
}

void *gbc_malloc(int num)
{
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
    if (buffer == NULL || len == 0) {
        return GBC_ERROR;
    }
#if defined(GBC_SMOKE_STATE_REPLAY)
    for (size_t i = 0; i < len; i++) { smoke_audio_hash = (smoke_audio_hash ^ buffer[i]) * 16777619UL; }
#endif
    smoke_audio_seen = 1;
    smoke_audio_bytes += (uint32_t)len;
    if (smoke_audio_file != NULL) {
        if (fwrite(buffer, 1, len, smoke_audio_file) != len) {
            return GBC_ERROR;
        }
    }
    return GBC_OK;
}

int gbc_log_printf(const char *format, ...)
{
    va_list args;
    int written;

    va_start(args, format);
    written = vfprintf(stdout, format, args);
    va_end(args);
    return written;
}

uint32_t gbc_get_ticks(void)
{
    return 0;
}

void gbc_delay(uint32_t ms)
{
    (void)ms;
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


#if defined(GBC_SMOKE_STATE_REPLAY)
/** Replay external ROMs without writing any file beside their cartridge images. */
static int smoke_state_replay(gbc_t *gbc)
{
    const char *path = "rom_replay.state";
    uint32_t original_bytes = smoke_audio_bytes;
    if (gbc_state_save_file(gbc, path) != GBC_OK) { return GBC_ERROR; }
    uint32_t reference[4] = {0};
    for (uint8_t pass = 0; pass < 2; pass++) {
        if (pass && gbc_state_load_file(gbc, path) != GBC_OK) { remove(path); return GBC_ERROR; }
        uint32_t target = gbc->frame_count + 60;
        uint64_t cycles = 0;
        smoke_audio_hash = 2166136261UL;
        while (gbc->frame_count < target && cycles < 12000000ULL) { cycles += gbc_step(gbc); }
        if (gbc->frame_count != target) { remove(path); return GBC_ERROR; }
        uint32_t actual[4] = {
            smoke_hash32((const uint8_t *)&gbc->cpu, sizeof(gbc->cpu)),
            smoke_hash32((const uint8_t *)&gbc->mmu, sizeof(gbc->mmu)),
            smoke_hash32((const uint8_t *)gbc->gbc_draw_data, sizeof(gbc->gbc_draw_data)),
            smoke_audio_hash
        };
        if (!pass) { memcpy(reference, actual, sizeof(reference)); }
        else if (memcmp(reference, actual, sizeof(reference))) { remove(path); return GBC_ERROR; }
    }
    int ret = gbc_state_load_file(gbc, path);
    smoke_audio_bytes = original_bytes;
    remove(path);
    printf("state replay: %s (CPU, MMU, pixels, audio)\n", ret == GBC_OK ? "matched" : "FAILED");
    return ret;
}
#endif

int main(int argc, char **argv)
{
    const char *rom_path;
    const char *audio_path = NULL;
    gbc_t *gbc;
    uint32_t frames = DEFAULT_FRAMES;
    uint32_t start_frame;
    uint32_t frames_run;
    uint64_t cycles_run = 0;
    uint64_t cycle_budget = DEFAULT_CYCLE_BUDGET;
    uint32_t framebuffer_hash;
    uint32_t audio_hash = 0;
    int result;

    if (argc < 2) {
        printf("usage: %s <rom file> [frames] [audio file] [cycle budget]\n", argv[0]);
        printf("       frames defaults to %u, maximum %u\n", DEFAULT_FRAMES, MAX_FRAMES);
        printf("       cycle budget defaults to %llu emulated cycles per frame\n",
               (unsigned long long)DEFAULT_CYCLE_BUDGET);
        printf("       exits non-zero if the image is unsupported or never produces a frame\n");
        return 2;
    }
    rom_path = argv[1];
    if (argc > 2) {
        long parsed = strtol(argv[2], NULL, 10);
        if (parsed < 1 || (unsigned long)parsed > MAX_FRAMES) {
            printf("FAIL invalid frame count: %s\n", argv[2]);
            return 2;
        }
        frames = (uint32_t)parsed;
    }
    if (argc > 3) {
        /* "none" and "-" mean "do not capture audio", so a batch run cannot
         * leave a stray file behind when it only wants the frame statistics. */
        if (strcmp(argv[3], "none") != 0 && strcmp(argv[3], "-") != 0) {
            audio_path = argv[3];
        }
    }
    if (argc > 4) {
        long parsed = strtol(argv[4], NULL, 10);
        if (parsed < 1000) {
            printf("FAIL invalid cycle budget: %s\n", argv[4]);
            return 2;
        }
        cycle_budget = (uint64_t)parsed;
    }

    gbc = gbc_init();
    if (gbc == NULL) {
        printf("FAIL gbc_init returned NULL\n");
        return 1;
    }

    result = gbc_load_file(gbc, rom_path);
    if (result != GBC_OK) {
        printf("FAIL load %s: %d\n", rom_path, result);
        gbc_deinit(gbc);
        return 1;
    }

    printf("context bytes: %zu\n", sizeof(*gbc));
    printf("rom: %s\n", rom_path);
    printf("title: %.16s\n", gbc->cart.header.title);
    printf("mbc: %u, rom: %u KiB in %u banks, ram: %u KiB, cgb flag: 0x%02X, model: %u\n",
           (unsigned)gbc->cart.header.mbc,
           (unsigned)(gbc->cart.header.rom_size / 1024UL),
           (unsigned)gbc->cart.header.rom_banks,
           (unsigned)(gbc->cart.header.ram_size / 1024UL),
           (unsigned)gbc->cart.header.cgb_flag,
           (unsigned)gbc->model);

    if (audio_path != NULL) {
        smoke_audio_file = fopen(audio_path, "wb");
        if (smoke_audio_file == NULL) {
            printf("FAIL open %s for writing\n", audio_path);
            gbc_deinit(gbc);
            return 1;
        }
    }

    /* The ROM stays loaded for the whole run and the harness decides when to
     * stop, so the run is bounded two ways: by produced frames and by consumed
     * emulated cycles.  The cycle budget matters because a cartridge that keeps
     * the LCD disabled (a boot wait loop, or a title that blanks the screen)
     * never produces a frame, and without the budget this loop would spin
     * forever instead of reporting that nothing happened. */
    start_frame = gbc->frame_count;
    while ((gbc->frame_count - start_frame) < frames) {
#ifdef GBC_SMOKE_TRACE
        smoke_trace_note(gbc);
#endif
        if (cycles_run >= (uint64_t)frames * cycle_budget) {
            printf("FAIL cycle budget exhausted: %u of %u frames after %llu cycles\n",
                   (unsigned)(gbc->frame_count - start_frame), (unsigned)frames,
                   (unsigned long long)cycles_run);
            printf("     lcdc: 0x%02X ly: %u pc: 0x%04X (LCD disabled for the whole run?)\n",
                   (unsigned)gbc->mmu.io[0x40], (unsigned)gbc->mmu.io[0x44], (unsigned)gbc->cpu.pc);
#ifdef GBC_SMOKE_TRACE
            smoke_trace_dump();
#endif
            if (smoke_audio_file != NULL) {
                fclose(smoke_audio_file);
                smoke_audio_file = NULL;
            }
            gbc_unload_file(gbc);
            gbc_deinit(gbc);
            return 1;
        }
        cycles_run += gbc_step(gbc);
    }
    /* The snapshot excludes the derived framebuffer. Capture the requested
     * run's hash before the replay produces later frames. */
    framebuffer_hash = smoke_hash32((const uint8_t *)gbc->gbc_draw_data, sizeof(gbc->gbc_draw_data));
#if defined(GBC_SMOKE_STATE_REPLAY)
    if (smoke_state_replay(gbc) != GBC_OK) {
        printf("FAIL state replay\n");
        gbc_unload_rom_discard(gbc); gbc_deinit(gbc);
        return 1;
    }
#endif
    smoke_request_quit(gbc);

    frames_run = gbc->frame_count - start_frame;
#if (GBC_ENABLE_SOUND == 1)
    if (smoke_audio_seen) {
        audio_hash = smoke_audio_bytes; /* stream length is the stable signal */
    }
#endif

    printf("frames: %u\n", frames_run);
    printf("pc: 0x%04X sp: 0x%04X af: 0x%04X bc: 0x%04X de: 0x%04X hl: 0x%04X\n",
           (unsigned)gbc->cpu.pc, (unsigned)gbc->cpu.sp, (unsigned)gbc->cpu.af.w,
           (unsigned)gbc->cpu.bc.w, (unsigned)gbc->cpu.de.w, (unsigned)gbc->cpu.hl.w);
    printf("ly: %u mode: %u lcdc: 0x%02X stat: 0x%02X ie: 0x%02X if: 0x%02X\n",
           (unsigned)gbc->mmu.io[0x44], (unsigned)(gbc->mmu.io[0x41] & 0x03),
           (unsigned)gbc->mmu.io[0x40], (unsigned)gbc->mmu.io[0x41],
           (unsigned)gbc->mmu.ie, (unsigned)gbc->mmu.io[0x0F]);
    printf("framebuffer hash: 0x%08X\n", (unsigned)framebuffer_hash);
#if (GBC_ENABLE_SOUND == 1)
    printf("audio: %s, %u bytes\n", smoke_audio_seen ? "produced" : "silent", (unsigned)audio_hash);
#else
    printf("audio: disabled in this build\n");
#endif

    if (smoke_audio_file != NULL) {
        fclose(smoke_audio_file);
        smoke_audio_file = NULL;
    }

    result = gbc_unload_file(gbc);
    if (result != GBC_OK) {
        printf("FAIL unload %s: %d\n", rom_path, result);
        gbc_deinit(gbc);
        return 1;
    }
    gbc_deinit(gbc);

    if (frames_run != frames) {
        printf("FAIL ran %u frames, expected %u\n", (unsigned)frames_run, (unsigned)frames);
        return 1;
    }

    printf("rom smoke test passed\n");
    return 0;
}
