/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "gbc.h"

static void gbc_reset(gbc_t *gbc)
{
    gbc_model_t model = gbc->model;

    gbc->gbc_quit = 0;
#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)
    gbc->state_fault = 0;
#endif
    gbc->frame_count = 0;
    gbc->last_frame_ticks = 0;
    gbc->model = model;
    gbc_cart_reset(gbc);
    gbc_cart_rebuild_mapping(gbc);
    gbc_mmu_reset(gbc);
    gbc_timer_reset(gbc);
    gbc_joypad_reset(gbc);
    gbc_ppu_reset(gbc);
    gbc_apu_reset(gbc);
    gbc_cpu_reset(gbc);
#if (GBC_ENABLE_BOOT_ROM == 1)
    gbc_boot_reset(gbc);
#endif
}

/** Initialize caller-owned emulator state without allocating its context. */
int gbc_init_static(gbc_t *gbc)
{
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    gbc_memset(gbc, 0, sizeof(*gbc));
    gbc->model = GBC_MODEL_DMG;
#if (GBC_ENABLE_CGB == 1)
    gbc->preferred_model = GBC_MODEL_CGB;
#else
    gbc->preferred_model = GBC_MODEL_DMG;
#endif
    gbc_reset(gbc);
    return gbc_initex(gbc);
}

gbc_t *gbc_init(void)
{
    gbc_t *gbc = (gbc_t *)gbc_malloc(sizeof(gbc_t));
    if (gbc == NULL) {
        return NULL;
    }
    if (gbc_init_static(gbc) != GBC_OK) {
        gbc_free(gbc);
        return NULL;
    }
    return gbc;
}

/** Release cartridge/platform resources while retaining caller-owned storage. */
int gbc_deinit_static(gbc_t *gbc)
{
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    int ret = gbc_unload_rom(gbc);
    if (ret != GBC_OK) {
        return ret;
    }
    return gbc_deinitex(gbc);
}

int gbc_deinit(gbc_t *gbc)
{
    int ret;
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    ret = gbc_deinit_static(gbc);
    if (ret != GBC_OK) {
        return ret;
    }
    gbc_free(gbc);
    return ret;
}

int gbc_load_rom(gbc_t *gbc, const uint8_t *rom, uint32_t size)
{
    int ret;

    if (gbc == NULL) {
        return GBC_ERROR_BAD_ROM;
    }
    ret = gbc_cart_load_rom(gbc, rom, size, 0);
    if (ret != GBC_OK) {
        GBC_LOG_ERROR("gbc_load_rom failed: %d\n", ret);
        return ret;
    }
    gbc_reset(gbc);
    return GBC_OK;
}

int gbc_unload_rom(gbc_t *gbc)
{
#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)
    if (gbc && gbc->state_fault) { return GBC_ERROR_FORMAT; }
#endif
    if (gbc == NULL) {
        return GBC_ERROR;
    }
#if (GBC_PERSIST_ENABLE)
    int ret = gbc_persist_save(gbc);
    if (ret != GBC_OK) {
        return ret;
    }
#endif
    return gbc_unload_rom_discard(gbc);
}

int gbc_unload_rom_discard(gbc_t *gbc)
{
    if (gbc == NULL) {
        return GBC_ERROR;
    }
#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)
    gbc->state_fault = 0;
#endif
    gbc_cart_unload(gbc);
    return GBC_OK;
}

#if (GBC_USE_FS == 1)
int gbc_load_file(gbc_t *gbc, const char *file_path)
{
    int ret;

    if (gbc == NULL || file_path == NULL) {
        return GBC_ERROR_IO;
    }
    ret = gbc_cart_load_file(gbc, file_path);
    if (ret != GBC_OK) {
        GBC_LOG_ERROR("gbc_load_file failed: %d\n", ret);
        return ret;
    }
    gbc_reset(gbc);
#if (GBC_PERSIST_ENABLE)
    ret = gbc_persist_set_path(gbc, file_path);
    if (ret == GBC_OK) {
        ret = gbc_persist_load(gbc);
    }
    if (ret != GBC_OK) {
        gbc->cart.persist_blocked = 1;
        GBC_LOG_WARN("persistence load failed: %d; cartridge retained, saves blocked\n", ret);
        return ret;
    }
#endif
    return GBC_OK;
}

int gbc_unload_file(gbc_t *gbc)
{
    return gbc_unload_rom(gbc);
}
#endif

uint8_t gbc_step(gbc_t *gbc)
{
    uint8_t cycles;
#if (GBC_HALT_BATCH_CYCLES > 4)
    uint8_t total = 0;
#endif

    if (gbc == NULL || gbc->gbc_quit || gbc->cart.rom_data == NULL) {
        return 0;
    }
#if (GBC_HALT_BATCH_CYCLES > 4)
    do {
        if (total != 0) {
            /* Only enter for an idle HALT with no pending enabled interrupt. */
            gbc->cpu.cycles += 4;
            cycles = 4;
        } else
#endif
        cycles = gbc_cpu_step(gbc);
    /* In CGB fast mode, the CPU runs at 8 MHz while PPU/APU still
     * run at 4 MHz. DIV/TIMA and serial follow the CPU clock.
     * Halve the cycles passed to PPU/APU so they advance
     * at the correct rate relative to wall time. */
    {
        uint8_t sub_cycles = gbc->mmu.double_speed ? (uint8_t)(cycles >> 1) : cycles;
        /* The HDMA stall is measured in CPU cycles: in fast mode the same
         * number of them covers half the wall time. */
        gbc_mmu_step_dma(gbc, cycles);
        gbc_mmu_step_serial(gbc, cycles);
        gbc_timer_step(gbc, cycles);
        gbc_ppu_step(gbc, sub_cycles);
        gbc_apu_step(gbc, sub_cycles);
    }

#if (GBC_HALT_BATCH_CYCLES > 4)
        total = (uint8_t)(total + cycles);
    } while (!gbc->gbc_quit && gbc->cpu.halted &&
             gbc->mmu.hdma_busy_cycles == 0 && !gbc->ppu.frame_ready &&
             (gbc->mmu.ie & gbc->mmu.io[0x0F] & 0x1F) == 0 &&
             total <= GBC_HALT_BATCH_CYCLES - 4);
#endif

    if (gbc->ppu.frame_ready) {
        gbc->ppu.frame_ready = 0;
        gbc->frame_count++;
#if (GBC_FRAME_SKIP != 0)
        if (gbc->gbc_frame_skip_count >= GBC_FRAME_SKIP) {
            gbc->gbc_frame_skip_count = 0;
        } else {
            gbc->gbc_frame_skip_count++;
        }
#endif
        gbc_frame(gbc);
    }
#if (GBC_HALT_BATCH_CYCLES > 4)
    return total;
#else
    return cycles;
#endif
}

int gbc_set_model(gbc_t *gbc, gbc_model_t model)
{
    if (gbc == NULL) {
        return GBC_ERROR;
    }
    if (model != GBC_MODEL_DMG && model != GBC_MODEL_CGB) { return GBC_ERROR_UNSUPPORTED; }
#if (GBC_ENABLE_BOOT_ROM == 1)
    if (gbc->boot_rom && (gbc->boot_size == 256 ? GBC_MODEL_DMG : GBC_MODEL_CGB) != model) { return GBC_ERROR_MISMATCH; }
#endif
    if (model == GBC_MODEL_CGB) {
#if (GBC_ENABLE_CGB == 0)
        return GBC_ERROR_UNSUPPORTED;
#endif
    } else {
#if (GBC_ENABLE_DMG == 0)
        return GBC_ERROR_UNSUPPORTED;
#endif
    }

    if (gbc->cart.rom_data != NULL) {
        if (gbc->cart.header.mode == GBC_CART_MODE_CGB_ONLY && model == GBC_MODEL_DMG) {
            return GBC_ERROR_UNSUPPORTED;
        }
        if (gbc->cart.header.mode == GBC_CART_MODE_DMG_ONLY && model == GBC_MODEL_CGB) {
            return GBC_ERROR_UNSUPPORTED;
        }
    }

    gbc->preferred_model = model;
    gbc->model = model;
    if (gbc->cart.rom_data != NULL) {
        gbc_reset(gbc);
    }
    return GBC_OK;
}

void gbc_run(gbc_t *gbc)
{
    if (gbc == NULL) {
        return;
    }
    while (!gbc->gbc_quit) {
        gbc_step(gbc);
    }
}

