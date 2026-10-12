/*
 * Copyright PeakRacing
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "gbc.h"
#if (GBC_ENABLE_BOOT_ROM == 1)
int gbc_boot_set_rom(gbc_t *gbc, const uint8_t *rom, uint32_t size, gbc_model_t hardware)
{
    if (!gbc || gbc->cart.rom_data) { return GBC_ERROR; }
    if (!rom && !size) { gbc->boot_rom = NULL; gbc->boot_size = 0; gbc->boot_crc = 0; gbc->boot_active = 0; return GBC_OK; }
    if (!rom || (hardware != GBC_MODEL_DMG && hardware != GBC_MODEL_CGB)) { return GBC_ERROR_FORMAT; }
    if ((hardware == GBC_MODEL_DMG && (size != 256 || !GBC_ENABLE_DMG)) ||
        (hardware == GBC_MODEL_CGB && ((size != 2048 && size != 2304) || !GBC_ENABLE_CGB))) { return GBC_ERROR_UNSUPPORTED; }
    /* Normalize the CGB address gap so compact/padded copies share identity. */
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < (hardware == GBC_MODEL_DMG ? 256U : 2048U); i++) {
        uint32_t offset = (size == 2304 && i >= 256) ? i + 256 : i;
        crc ^= rom[offset];
        for (uint8_t bit = 0; bit < 8; bit++) { crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0); }
    }
    gbc->boot_rom = rom; gbc->boot_size = size; gbc->boot_crc = ~crc;
    gbc->hardware_model = hardware; gbc->preferred_model = hardware;
    return GBC_OK;
}

void gbc_boot_reset(gbc_t *gbc)
{
    gbc->boot_active = 0; gbc->boot_compat = 0; gbc->boot_key0 = 0; gbc->boot_opri = gbc->model == GBC_MODEL_DMG;
    if (!gbc->boot_rom) { gbc->hardware_model = gbc->model; return; }
    /* A post-boot snapshot may restore another hardware model while retaining
     * a borrowed image. The bound image's size remains authoritative on reset. */
    gbc->hardware_model = gbc->boot_size == 256 ? GBC_MODEL_DMG : GBC_MODEL_CGB;
    gbc->boot_active = 1; gbc->model = gbc->hardware_model; gbc->boot_opri = 0;
    /* Deterministic power-up state, separate from post-boot presets. Undefined
     * hardware RAM values are intentionally zeroed; no boot animation is synthesized. */
    gbc_memset(&gbc->cpu, 0, sizeof(gbc->cpu));
    gbc_memset(gbc->mmu.io, 0, sizeof(gbc->mmu.io));
    gbc->mmu.io[0x00] = 0xCF; gbc->mmu.io[0x0F] = 0xE1;
    gbc->mmu.io[0x41] = 0x80; gbc->mmu.io[0x55] = 0xFF;
    gbc_memset(&gbc->ppu, 0, sizeof(gbc->ppu));
    gbc_memset(gbc->ppu.bg_palette, 0xFF, sizeof(gbc->ppu.bg_palette));
    gbc_memset(gbc->ppu.obj_palette, 0xFF, sizeof(gbc->ppu.obj_palette));
    gbc_timer_reset(gbc); gbc_apu_reset(gbc);
}

void gbc_boot_unmap(gbc_t *gbc)
{
    if (!gbc->boot_active) { return; }
    gbc->boot_active = 0;
    gbc->boot_compat = gbc->hardware_model == GBC_MODEL_CGB && (gbc->boot_key0 & 4) != 0;
    gbc->model = gbc->boot_compat ? GBC_MODEL_DMG : gbc->hardware_model;
    if (gbc->model == GBC_MODEL_DMG) {
        gbc->mmu.vram_bank = 0; gbc->mmu.wram_bank = 1;
        gbc->mmu.double_speed = 0; gbc->mmu.speed_prepare = 0;
        gbc->mmu.hdma_active = 0; gbc->mmu.hdma_blocks = 0; gbc->mmu.hdma_busy_cycles = 0;
        gbc->mmu.io[0x55] = 0xFF;
    }
}
#endif
