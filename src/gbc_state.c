/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "gbc_storage.h"
#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)

typedef struct {
    size_t offset;
    uint32_t count;
    uint8_t native_width, wire_width;
    uint32_t minimum, maximum, mask;
} gbc_state_field_t;

/* One bounded data buffer per save/load; filenames are separate stack storage. */
typedef struct {
    FILE *fp;
    uint32_t remaining, crc;
    uint16_t cursor, available;
#if (GBC_ENABLE_BOOT_ROM == 1)
    uint8_t saved_model, saved_hardware, saved_active, saved_compat;
    uint8_t expected_active;
#endif
    uint8_t phase; /* 0: encode, 1: validate, 2: apply */
    int error;
    uint8_t buffer[256];
} gbc_state_io_t;

static const gbc_state_field_t gbc_state_fields1[] = {
    {offsetof(gbc_t, cpu.af.w), 1, sizeof(uint16_t), 2, 0, 65535UL, 0xFFF0},
    {offsetof(gbc_t, cpu.bc.w), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, cpu.de.w), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, cpu.hl.w), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, cpu.sp), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, cpu.pc), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, cpu.ime), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, cpu.ime_delay), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, cpu.halted), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, cpu.halt_bug), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, cpu.stopped), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, cpu.last_opcode), 1, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, cpu.cycles), 1, sizeof(uint32_t), 4, 0, 4294967295UL, 4294967295UL},
};
static const gbc_state_field_t gbc_state_fields2[] = {
    {offsetof(gbc_t, mmu.vram), sizeof(((gbc_t *)0)->mmu.vram), sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, mmu.wram), sizeof(((gbc_t *)0)->mmu.wram), sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, mmu.oam), 0xA0, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, mmu.io), 0x80, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, mmu.hram), 0x7F, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, mmu.ie), 1, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, mmu.vram_bank), 1, sizeof(uint8_t), 1, 0, GBC_VRAM_BANKS - 1, 255UL},
    {offsetof(gbc_t, mmu.wram_bank), 1, sizeof(uint8_t), 1, 1, GBC_WRAM_BANKS - 1, 255UL},
    {offsetof(gbc_t, mmu.double_speed), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, mmu.speed_prepare), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, mmu.hdma_active), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, mmu.hdma_src), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, mmu.hdma_dst), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, mmu.hdma_blocks), 1, sizeof(uint8_t), 1, 0, 128, 255UL},
    {offsetof(gbc_t, mmu.hdma_busy_cycles), 1, sizeof(uint8_t), 1, 0, 64, 255UL},
    {offsetof(gbc_t, mmu.oam_dma_cycles), 1, sizeof(uint16_t), 2, 0, 640, 65535UL},
    {offsetof(gbc_t, mmu.serial_cycles), 1, sizeof(uint16_t), 2, 0, 4096, 65535UL},
};
static const gbc_state_field_t gbc_state_fields3[] = {
    {offsetof(gbc_t, timer.div_counter), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, timer.tima_counter), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, timer.tima_reload_delay), 1, sizeof(uint8_t), 1, 0, 4, 255UL},
};
static const gbc_state_field_t gbc_state_fields4[] = {
    {offsetof(gbc_t, joypad.state), 1, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, joypad.select), 1, sizeof(uint8_t), 1, 0, 255UL, 255UL},
};
static const gbc_state_field_t gbc_state_fields5[] = {
    {offsetof(gbc_t, ppu.dot_cycles), 1, sizeof(uint16_t), 2, 0, 455, 65535UL},
    {offsetof(gbc_t, ppu.mode), 1, sizeof(uint8_t), 1, 0, 3, 255UL},
    {offsetof(gbc_t, ppu.frame_ready), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, ppu.win_line), 1, sizeof(uint8_t), 1, 0, 144, 255UL},
    {offsetof(gbc_t, ppu.bgpi), 1, sizeof(uint8_t), 1, 0, 255, 255UL},
    {offsetof(gbc_t, ppu.obpi), 1, sizeof(uint8_t), 1, 0, 255, 255UL},
    {offsetof(gbc_t, ppu.bg_palette), 64, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, ppu.obj_palette), 64, sizeof(uint8_t), 1, 0, 255UL, 255UL},
};
#if (GBC_ENABLE_SOUND == 1)
static const gbc_state_field_t gbc_state_fields6[] = {
    {offsetof(gbc_t, apu.enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.frame_step), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.frame_acc), 1, sizeof(uint32_t), 4, 0, 4294967295UL, 4294967295UL},
    {offsetof(gbc_t, apu.sample_acc), 1, sizeof(uint32_t), 4, 0, 4294967295UL, 4294967295UL},
    {offsetof(gbc_t, apu.sample_buffer), GBC_APU_BUFFER_SAMPLES * GBC_APU_SAMPLE_BYTES, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, apu.sample_index), 1, sizeof(uint16_t), 2, 0, GBC_APU_BUFFER_SAMPLES * GBC_APU_SAMPLE_BYTES - 4, 65532UL},
    {offsetof(gbc_t, apu.square1.enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square1.dac_enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square1.duty), 1, sizeof(uint8_t), 1, 0, 3, 255UL},
    {offsetof(gbc_t, apu.square1.duty_step), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.square1.length_counter), 1, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, apu.square1.length_enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square1.envelope_volume), 1, sizeof(uint8_t), 1, 0, 15, 255UL},
    {offsetof(gbc_t, apu.square1.envelope_period), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.square1.envelope_timer), 1, sizeof(uint8_t), 1, 0, 8, 255UL},
    {offsetof(gbc_t, apu.square1.envelope_add), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square1.frequency), 1, sizeof(uint16_t), 2, 0, 2047, 65535UL},
    {offsetof(gbc_t, apu.square1.timer), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, apu.square1.shadow_frequency), 1, sizeof(uint16_t), 2, 0, 2047, 65535UL},
    {offsetof(gbc_t, apu.square1.sweep_period), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.square1.sweep_timer), 1, sizeof(uint8_t), 1, 0, 8, 255UL},
    {offsetof(gbc_t, apu.square1.sweep_negate), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square1.sweep_negate_used), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square1.sweep_shift), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.square2.enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square2.dac_enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square2.duty), 1, sizeof(uint8_t), 1, 0, 3, 255UL},
    {offsetof(gbc_t, apu.square2.duty_step), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.square2.length_counter), 1, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, apu.square2.length_enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square2.envelope_volume), 1, sizeof(uint8_t), 1, 0, 15, 255UL},
    {offsetof(gbc_t, apu.square2.envelope_period), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.square2.envelope_timer), 1, sizeof(uint8_t), 1, 0, 8, 255UL},
    {offsetof(gbc_t, apu.square2.envelope_add), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square2.frequency), 1, sizeof(uint16_t), 2, 0, 2047, 65535UL},
    {offsetof(gbc_t, apu.square2.timer), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, apu.square2.shadow_frequency), 1, sizeof(uint16_t), 2, 0, 2047, 65535UL},
    {offsetof(gbc_t, apu.square2.sweep_period), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.square2.sweep_timer), 1, sizeof(uint8_t), 1, 0, 8, 255UL},
    {offsetof(gbc_t, apu.square2.sweep_negate), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square2.sweep_negate_used), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.square2.sweep_shift), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.wave.enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.wave.dac_enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.wave.length_counter), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, apu.wave.length_enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.wave.volume_code), 1, sizeof(uint8_t), 1, 0, 3, 255UL},
    {offsetof(gbc_t, apu.wave.position), 1, sizeof(uint8_t), 1, 0, 31, 255UL},
    {offsetof(gbc_t, apu.wave.frequency), 1, sizeof(uint16_t), 2, 0, 2047, 65535UL},
    {offsetof(gbc_t, apu.wave.timer), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, apu.noise.enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.noise.dac_enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.noise.length_counter), 1, sizeof(uint8_t), 1, 0, 255UL, 255UL},
    {offsetof(gbc_t, apu.noise.length_enabled), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.noise.envelope_volume), 1, sizeof(uint8_t), 1, 0, 15, 255UL},
    {offsetof(gbc_t, apu.noise.envelope_period), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.noise.envelope_timer), 1, sizeof(uint8_t), 1, 0, 8, 255UL},
    {offsetof(gbc_t, apu.noise.envelope_add), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.noise.clock_shift), 1, sizeof(uint8_t), 1, 0, 15, 255UL},
    {offsetof(gbc_t, apu.noise.width_mode), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, apu.noise.divisor_code), 1, sizeof(uint8_t), 1, 0, 7, 255UL},
    {offsetof(gbc_t, apu.noise.timer), 1, sizeof(uint32_t), 4, 0, 4294967295UL, 4294967295UL},
    {offsetof(gbc_t, apu.noise.lfsr), 1, sizeof(uint16_t), 2, 0, 32767, 65535UL},
};
#endif
static const gbc_state_field_t gbc_state_fields7[] = {
    {offsetof(gbc_t, cart.ram_enable), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, cart.banking_mode), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, cart.ram_bank), 1, sizeof(uint8_t), 1, 0, 15, 255UL},
    {offsetof(gbc_t, cart.mbc1_low5), 1, sizeof(uint8_t), 1, 0, 31, 255UL},
    {offsetof(gbc_t, cart.mbc1_high2), 1, sizeof(uint8_t), 1, 0, 3, 255UL},
    {offsetof(gbc_t, cart.rtc_select), 1, sizeof(uint8_t), 1, 0, 255, 255UL},
    {offsetof(gbc_t, cart.rtc_latch_prev), 1, sizeof(uint8_t), 1, 0, 1, 255UL},
    {offsetof(gbc_t, cart.rom_bank), 1, sizeof(uint16_t), 2, 0, 65535UL, 65535UL},
    {offsetof(gbc_t, cart.mbc2_ram), 512, sizeof(uint8_t), 1, 0, 15, 255UL},
    {offsetof(gbc_t, cart.rtc_regs[0]), 1, sizeof(uint8_t), 1, 0, 59, 255UL},
    {offsetof(gbc_t, cart.rtc_regs[1]), 1, sizeof(uint8_t), 1, 0, 59, 255UL},
    {offsetof(gbc_t, cart.rtc_regs[2]), 1, sizeof(uint8_t), 1, 0, 23, 255UL},
    {offsetof(gbc_t, cart.rtc_regs[3]), 1, sizeof(uint8_t), 1, 0, 255, 255UL},
    {offsetof(gbc_t, cart.rtc_regs[4]), 1, sizeof(uint8_t), 1, 0, 255, 0xC1},
    {offsetof(gbc_t, cart.rtc_latched[0]), 1, sizeof(uint8_t), 1, 0, 59, 255UL},
    {offsetof(gbc_t, cart.rtc_latched[1]), 1, sizeof(uint8_t), 1, 0, 59, 255UL},
    {offsetof(gbc_t, cart.rtc_latched[2]), 1, sizeof(uint8_t), 1, 0, 23, 255UL},
    {offsetof(gbc_t, cart.rtc_latched[3]), 1, sizeof(uint8_t), 1, 0, 255, 255UL},
    {offsetof(gbc_t, cart.rtc_latched[4]), 1, sizeof(uint8_t), 1, 0, 255, 0xC1},
    {offsetof(gbc_t, cart.rtc_ms), 1, sizeof(uint16_t), 2, 0, 999, 65535UL},
};
#if (GBC_ENABLE_BOOT_ROM == 1)
static const gbc_state_field_t gbc_state_boot[] = {
    {offsetof(gbc_t, hardware_model), 1, sizeof(gbc_model_t), 4, 0, GBC_ENABLE_CGB, 1},
    {offsetof(gbc_t, boot_active), 1, 1, 1, 0, 1, 1},
    {offsetof(gbc_t, boot_key0), 1, 1, 1, 0, 255, 255},
    {offsetof(gbc_t, boot_opri), 1, 1, 1, 0, 1, 1},
    {offsetof(gbc_t, boot_compat), 1, 1, 1, 0, 1, 1},
};
#endif
static const gbc_state_field_t gbc_state_fields8[] = {
    {offsetof(gbc_t, model), 1, sizeof(gbc_model_t), 4, !GBC_ENABLE_DMG, GBC_ENABLE_CGB, 1},
    {offsetof(gbc_t, preferred_model), 1, sizeof(gbc_model_t), 4, !GBC_ENABLE_DMG, GBC_ENABLE_CGB, 1},
    {offsetof(gbc_t, frame_count), 1, sizeof(uint32_t), 4, 0, 4294967295UL, 4294967295UL},
};
#if (GBC_FRAME_SKIP != 0)
static const gbc_state_field_t gbc_state_skip[] = {
    {offsetof(gbc_t, gbc_frame_skip_count), 1, 1, 1, 0, GBC_FRAME_SKIP, 255},
};
#endif

static uint32_t gbc_state_get32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void gbc_state_put32(uint8_t *b, uint32_t v)
{
    for (uint8_t i = 0; i < 4; i++) { b[i] = (uint8_t)(v >> (8 * i)); }
}

/** Read/write byte sequences while retaining only one 256-byte buffer. */
static void gbc_state_bytes(gbc_state_io_t *io, uint8_t *data, uint32_t length)
{
    while (length && io->error == GBC_OK) {
        if (io->phase == 0) {
            uint32_t n = 256 - io->cursor;
            if (n > length) { n = length; }
            gbc_memcpy(io->buffer + io->cursor, data, n);
            io->crc = gbc_storage_crc(io->crc, data, n);
            io->cursor = (uint16_t)(io->cursor + n);
            if (io->cursor == 256) {
                if (gbc_fwrite(io->buffer, 1, 256, io->fp) != 256) { io->error = GBC_ERROR_IO; }
                io->cursor = 0;
            }
            data += n; length -= n;
        } else {
            if (io->cursor == io->available) {
                uint32_t n = io->remaining < 256 ? io->remaining : 256;
                if (!n) { io->error = GBC_ERROR_FORMAT; break; }
                if (gbc_fread(io->buffer, 1, n, io->fp) != n) { io->error = GBC_ERROR_IO; break; }
                io->remaining -= n;
                io->available = (uint16_t)n; io->cursor = 0;
            }
            uint32_t n = io->available - io->cursor;
            if (n > length) { n = length; }
            gbc_memcpy(data, io->buffer + io->cursor, n);
            io->crc = gbc_storage_crc(io->crc, data, n);
            io->cursor = (uint16_t)(io->cursor + n);
            data += n; length -= n;
        }
    }
}

static uint32_t gbc_state_fields_size(const gbc_state_field_t *fields, size_t count)
{
    uint32_t length = 0;
    for (size_t i = 0; i < count; i++) { length += fields[i].count * fields[i].wire_width; }
    return length;
}

/** Decode scalars by native type, never serialize structure padding/pointers. */
static void gbc_state_fields(gbc_state_io_t *io, gbc_t *gbc, const gbc_state_field_t *fields, size_t count)
{
    for (size_t i = 0; i < count && io->error == GBC_OK; i++) {
        const gbc_state_field_t *f = fields + i;
        uint8_t *ptr = (uint8_t *)gbc + f->offset;
        for (uint32_t j = 0; j < f->count && io->error == GBC_OK; j++, ptr += f->native_width) {
            uint32_t value = 0;
            uint8_t b[4] = {0};
            if (io->phase == 0) {
                if (f->native_width == 1) { value = *ptr; }
                else if (f->native_width == 2) { uint16_t n; gbc_memcpy(&n, ptr, 2); value = n; }
                else if (f->native_width == 4) { gbc_memcpy(&value, ptr, 4); }
                else { io->error = GBC_ERROR_UNSUPPORTED; return; }
                gbc_state_put32(b, value);
            }
            gbc_state_bytes(io, b, f->wire_width);
            if (io->phase != 0 && io->error == GBC_OK) {
                value = gbc_state_get32(b);
                if (value < f->minimum || value > f->maximum || (value & ~f->mask)) {
                    io->error = GBC_ERROR_FORMAT; return;
                }
#if (GBC_ENABLE_BOOT_ROM == 1)
                if (f->offset == offsetof(gbc_t, model)) { io->saved_model = (uint8_t)value; }
                if (f->offset == offsetof(gbc_t, hardware_model)) { io->saved_hardware = (uint8_t)value; }
                if (f->offset == offsetof(gbc_t, boot_active)) { io->saved_active = (uint8_t)value; }
                if (f->offset == offsetof(gbc_t, boot_compat)) { io->saved_compat = (uint8_t)value; }
#endif
                if (io->phase == 2) {
                    if (f->native_width == 1) { *ptr = (uint8_t)value; }
                    else if (f->native_width == 2) { uint16_t n = (uint16_t)value; gbc_memcpy(ptr, &n, 2); }
                    else { gbc_memcpy(ptr, &value, 4); }
                }
            }
        }
    }
}

static void gbc_state_chunk(gbc_state_io_t *io, uint32_t id, uint32_t length)
{
    if (io->error != GBC_OK) { return; }
    uint8_t bytes[8];
    gbc_state_put32(bytes, id); gbc_state_put32(bytes + 4, length);
    gbc_state_bytes(io, bytes, 8);
    if (gbc_state_get32(bytes) != id || gbc_state_get32(bytes + 4) != length) { io->error = GBC_ERROR_FORMAT; }
}

#define GBC_STATE_TABLE(io, gbc, id, table) do { \
    size_t count = sizeof(table) / sizeof(table[0]); \
    gbc_state_chunk(io, id, gbc_state_fields_size(table, count)); \
    gbc_state_fields(io, gbc, table, count); \
} while (0)
#define GBC_STATE_TABLE_SIZE(table) (8 + gbc_state_fields_size(table, sizeof(table) / sizeof(table[0])))

static uint32_t gbc_state_payload_size(gbc_t *gbc)
{
    uint32_t length = 8 + gbc->cart.ram_size;
    length += GBC_STATE_TABLE_SIZE(gbc_state_fields1);
    length += GBC_STATE_TABLE_SIZE(gbc_state_fields2);
    length += GBC_STATE_TABLE_SIZE(gbc_state_fields3);
    length += GBC_STATE_TABLE_SIZE(gbc_state_fields4);
    length += GBC_STATE_TABLE_SIZE(gbc_state_fields5);
#if (GBC_ENABLE_SOUND == 1)
    length += GBC_STATE_TABLE_SIZE(gbc_state_fields6);
#endif
    length += GBC_STATE_TABLE_SIZE(gbc_state_fields7);
    length += GBC_STATE_TABLE_SIZE(gbc_state_fields8);
#if (GBC_ENABLE_BOOT_ROM == 1)
    length += GBC_STATE_TABLE_SIZE(gbc_state_boot);
#endif
#if (GBC_FRAME_SKIP != 0)
    length += GBC_STATE_TABLE_SIZE(gbc_state_skip);
#endif
    return length;
}

static void gbc_state_payload(gbc_state_io_t *io, gbc_t *gbc)
{
    GBC_STATE_TABLE(io, gbc, 1, gbc_state_fields1);
    GBC_STATE_TABLE(io, gbc, 2, gbc_state_fields2);
    GBC_STATE_TABLE(io, gbc, 3, gbc_state_fields3);
    GBC_STATE_TABLE(io, gbc, 4, gbc_state_fields4);
    GBC_STATE_TABLE(io, gbc, 5, gbc_state_fields5);
#if (GBC_ENABLE_SOUND == 1)
    GBC_STATE_TABLE(io, gbc, 6, gbc_state_fields6);
#endif
    GBC_STATE_TABLE(io, gbc, 7, gbc_state_fields7);
    GBC_STATE_TABLE(io, gbc, 8, gbc_state_fields8);
#if (GBC_ENABLE_BOOT_ROM == 1)
    GBC_STATE_TABLE(io, gbc, 9, gbc_state_boot);
    if (io->phase && io->error == GBC_OK &&
        (io->saved_active != io->expected_active ||
         (io->saved_active && (io->saved_compat || io->saved_hardware != (gbc->boot_size == 256 ? GBC_MODEL_DMG : GBC_MODEL_CGB) || io->saved_model != io->saved_hardware)) ||
         (io->saved_compat && (io->saved_hardware != GBC_MODEL_CGB || io->saved_model != GBC_MODEL_DMG)) ||
         (!io->saved_active && !io->saved_compat && io->saved_model != io->saved_hardware))) {
        io->error = GBC_ERROR_FORMAT;
    }
#endif
#if (GBC_FRAME_SKIP != 0)
    GBC_STATE_TABLE(io, gbc, 11, gbc_state_skip);
#endif
    gbc_state_chunk(io, 10, gbc->cart.ram_size);
    if (io->phase == 1) {
        uint8_t byte;
        for (uint32_t i = 0; i < gbc->cart.ram_size && io->error == GBC_OK; i++) { gbc_state_bytes(io, &byte, 1); }
    } else {
        gbc_state_bytes(io, gbc->cart.ram_data, gbc->cart.ram_size);
    }
}

/** Hash the immutable cartridge once without changing banking or cache contents. */
static int gbc_state_rom_crc(gbc_t *gbc, uint32_t *out)
{
    gbc_cart_t *cart = &gbc->cart;
    if (!cart->rom_crc_valid) {
        uint32_t crc = 0xFFFFFFFFUL;
#if (GBC_ROM_STREAM == 1)
        if (cart->stream) {
            uint8_t buffer[256];
            long position = gbc_ftell(cart->stream);
            if (position < 0 || gbc_fseek(cart->stream, 0, SEEK_SET) != 0) { return GBC_ERROR_IO; }
            int ret = GBC_OK;
            for (uint32_t offset = 0; offset < cart->rom_size; offset += sizeof(buffer)) {
                size_t n = cart->rom_size - offset < sizeof(buffer) ? cart->rom_size - offset : sizeof(buffer);
                if (gbc_fread(buffer, 1, n, cart->stream) != n) { ret = GBC_ERROR_IO; break; }
                crc = gbc_storage_crc(crc, buffer, n);
            }
            if (gbc_fseek(cart->stream, position, SEEK_SET) != 0) { ret = GBC_ERROR_IO; }
            if (ret != GBC_OK) { return ret; }
        } else
#endif
        { crc = gbc_storage_crc(crc, cart->rom_data, cart->rom_size); }
        cart->rom_crc = ~crc;
        cart->rom_crc_valid = 1;
    }
    *out = cart->rom_crc;
    return GBC_OK;
}

static uint32_t gbc_state_config(void)
{
    /* Presentation/storage choices do not affect serialized emulation state. */
    uint32_t flags = GBC_ENABLE_SOUND | (GBC_ENABLE_DMG << 1) | (GBC_ENABLE_CGB << 2) | (GBC_USE_SRAM << 3) | (GBC_ENABLE_BOOT_ROM << 4);
    return flags;
}

static int gbc_state_header(gbc_t *gbc, uint8_t header[48])
{
    uint32_t crc;
    int ret = gbc_state_rom_crc(gbc, &crc);
    if (ret != GBC_OK) { return ret; }
    gbc_memset(header, 0, 48);
    gbc_memcpy(header, "GSTA", 4);
    gbc_state_put32(header + 4, 1);
    gbc_state_put32(header + 8, crc);
    gbc_state_put32(header + 12, gbc_state_config());
    gbc_state_put32(header + 16, gbc->cart.ram_size);
    gbc_state_put32(header + 20, gbc->cart.header.mbc);
#if (GBC_ENABLE_BOOT_ROM == 1)
    gbc_state_put32(header + 24, gbc->boot_active ? gbc->boot_crc : 0);
    gbc_state_put32(header + 44, gbc->boot_active ? 1 : 0);
#endif
    gbc_state_put32(header + 28, gbc_state_payload_size(gbc));
#if (GBC_ENABLE_SOUND == 1)
    gbc_state_put32(header + 32, GBC_APU_SAMPLE_RATE);
    gbc_state_put32(header + 36, GBC_APU_BUFFER_SAMPLES);
#endif
    gbc_state_put32(header + 40, GBC_FRAME_SKIP);
    return GBC_OK;
}

int gbc_state_save_file(gbc_t *gbc, const char *path)
{
    char temp[GBC_SAVE_PATH_MAX];
    uint8_t header[48], footer[4];
    if (!gbc || !path || !gbc->cart.rom_data) { return GBC_ERROR; }
    if (gbc->state_fault) { return GBC_ERROR_FORMAT; }
    int ret = gbc_storage_path(temp, path, ".tmp");
    if (ret == GBC_OK) { ret = gbc_state_header(gbc, header); }
    if (ret != GBC_OK) { return ret; }
    gbc_cart_rtc_sync(gbc);
    FILE *fp = gbc_fopen(temp, "wb");
    if (!fp) { return GBC_ERROR_IO; }
    gbc_state_io_t io = {0};
    io.fp = fp; io.crc = gbc_storage_crc(0xFFFFFFFFUL, header, sizeof(header));
    if (gbc_fwrite(header, 1, sizeof(header), fp) != sizeof(header)) { io.error = GBC_ERROR_IO; }
    gbc_state_payload(&io, gbc);
    if (io.error == GBC_OK && io.cursor && gbc_fwrite(io.buffer, 1, io.cursor, fp) != io.cursor) { io.error = GBC_ERROR_IO; }
    gbc_state_put32(footer, ~io.crc);
    if (io.error == GBC_OK && gbc_fwrite(footer, 1, sizeof(footer), fp) != sizeof(footer)) { io.error = GBC_ERROR_IO; }
    return gbc_storage_commit(fp, temp, path, io.error);
}

int gbc_state_load_file(gbc_t *gbc, const char *path)
{
    uint8_t header[48], expected[48], footer[4];
    if (!gbc || !path || !gbc->cart.rom_data) { return GBC_ERROR; }
    if (gbc->state_fault) { return GBC_ERROR_FORMAT; }
    int ret = gbc_state_header(gbc, expected);
    if (ret != GBC_OK) { return ret; }
    FILE *fp = gbc_fopen(path, "rb");
    if (!fp) { return GBC_ERROR_IO; }
    uint32_t length = gbc_state_get32(expected + 28);
    if (gbc_fseek(fp, 0, SEEK_END) != 0 || gbc_ftell(fp) != (long)(length + 52) || gbc_fseek(fp, 0, SEEK_SET) != 0) {
        ret = GBC_ERROR_FORMAT;
    } else if (gbc_fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        ret = GBC_ERROR_IO;
    }
#if (GBC_ENABLE_BOOT_ROM == 1)
    if (ret == GBC_OK) {
        uint32_t active = gbc_state_get32(header + 44);
        if (active > 1 || (active && (!gbc->boot_rom || gbc_state_get32(header + 24) != gbc->boot_crc)) ||
            (!active && gbc_state_get32(header + 24))) { ret = GBC_ERROR_MISMATCH; }
        gbc_state_put32(expected + 24, gbc_state_get32(header + 24));
        gbc_state_put32(expected + 44, active);
    }
#endif
    if (ret == GBC_OK && gbc_memcmp(header, expected, sizeof(header)) != 0) {
        ret = GBC_ERROR_MISMATCH;
    }
    gbc_state_io_t io = {0};
    for (uint8_t phase = 1; phase <= 2 && ret == GBC_OK; phase++) {
        if (phase == 2 && gbc_fseek(fp, 48, SEEK_SET) != 0) { ret = GBC_ERROR_IO; break; }
        gbc_memset(&io, 0, sizeof(io));
        io.fp = fp; io.phase = phase; io.remaining = length;
#if (GBC_ENABLE_BOOT_ROM == 1)
        io.expected_active = (uint8_t)gbc_state_get32(header + 44);
#endif
        io.crc = gbc_storage_crc(0xFFFFFFFFUL, header, sizeof(header));
        gbc_state_payload(&io, gbc);
        ret = io.error;
        if (ret == GBC_OK && (io.remaining || io.cursor != io.available)) { ret = GBC_ERROR_FORMAT; }
        if (ret == GBC_OK && gbc_fread(footer, 1, sizeof(footer), fp) != sizeof(footer)) { ret = GBC_ERROR_IO; }
        if (ret == GBC_OK && gbc_state_get32(footer) != ~io.crc) { ret = GBC_ERROR_FORMAT; }
        if (phase == 2 && ret != GBC_OK) { gbc->gbc_quit = 1; gbc->state_fault = 1; }
    }
    if (gbc_fclose(fp) != 0) {
        if (ret == GBC_OK) { gbc->gbc_quit = 1; gbc->state_fault = 1; }
        ret = GBC_ERROR_IO;
    }
    if (ret == GBC_OK) {
        gbc->cart.rtc_last_ticks = gbc_get_ticks(); gbc->cart.rtc_started = 1;
#if (GBC_ROM_STREAM == 1)
        for (uint8_t i = 0; i < GBC_ROM_CACHE_BANKS; i++) { gbc->cart.cache[i].valid = 0; }
        gbc->cart.cache_age = 0;
#endif
#if (GBC_PERSIST_ENABLE)
        gbc->cart.ram_dirty = 1;
#endif
        gbc->gbc_quit = 0; gbc->last_frame_ticks = 0;
        gbc_state_loaded(gbc);
    }
    return ret;
}
#endif
