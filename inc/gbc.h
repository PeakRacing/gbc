/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#pragma once

#include "gbc_default.h"
#include "gbc_cart.h"
#include "gbc_cpu.h"
#include "gbc_mmu.h"
#include "gbc_timer.h"
#include "gbc_joypad.h"
#include "gbc_ppu.h"
#include "gbc_apu.h"

#ifdef __cplusplus
    extern "C" {
#endif

#define GBC_VERSION_MAJOR           0
#define GBC_VERSION_MINOR           1
#define GBC_VERSION_PATCH           0
#define GBC_NAME                    "GBC"
#define GBC_URL                     "https://github.com/PeakRacing/gbc"

typedef struct gbc {
    uint8_t gbc_quit;
#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)
    uint8_t state_fault;
#endif
#if (GBC_FRAME_SKIP != 0)
    uint8_t gbc_frame_skip_count;
#endif
    gbc_model_t model;
    gbc_model_t preferred_model;
    uint32_t frame_count;
    uint32_t last_frame_ticks;
    gbc_cart_t cart;
    gbc_cpu_t cpu;
    gbc_mmu_t mmu;
    gbc_timer_t timer;
    gbc_joypad_t joypad;
    gbc_ppu_t ppu;
#if (GBC_ENABLE_SOUND == 1)
    gbc_apu_t apu;
#endif
#if (GBC_FRAMEBUFFER_ENABLE == 1)
    gbc_color_t gbc_draw_data[GBC_DRAW_SIZE];
#endif
} gbc_t;

/* Initialize caller-owned storage; release with gbc_deinit_static(). */
int gbc_init_static(gbc_t *gbc);
int gbc_deinit_static(gbc_t *gbc);
gbc_t *gbc_init(void);
int gbc_deinit(gbc_t *gbc);
void gbc_run(gbc_t *gbc);
uint8_t gbc_step(gbc_t *gbc);
int gbc_set_model(gbc_t *gbc, gbc_model_t model);

#if (GBC_USE_FS == 1)
int gbc_load_file(gbc_t *gbc, const char *file_path);
int gbc_unload_file(gbc_t *gbc);
#endif

#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)
int gbc_state_save_file(gbc_t *gbc, const char *path);
int gbc_state_load_file(gbc_t *gbc, const char *path);
/* Port hook: discard queued audio and reset presentation pacing. */
void gbc_state_loaded(gbc_t *gbc);
#endif

/* Borrow ROM storage without copying; keep it valid until unload. */
int gbc_load_rom(gbc_t *gbc, const uint8_t *rom, uint32_t size);
int gbc_unload_rom(gbc_t *gbc);
/* Explicitly discard unsaved cartridge changes, retaining the context. */
int gbc_unload_rom_discard(gbc_t *gbc);
#if (GBC_PERSIST_ENABLE)
/* Base path excludes .sav/.rtc. NULL detaches persistence. */
int gbc_persist_set_path(gbc_t *gbc, const char *base_path);
int gbc_persist_load(gbc_t *gbc);
int gbc_persist_save(gbc_t *gbc);
#endif
int gbc_initex(gbc_t *gbc);
int gbc_deinitex(gbc_t *gbc);
void gbc_frame(gbc_t *gbc);

#ifdef __cplusplus
    }
#endif

