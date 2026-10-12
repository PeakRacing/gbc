/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "gbc.h"
#if (GBC_ENABLE_FAST_RUN == 1)
typedef struct gbc_fast {
    uint8_t tiles[GBC_VRAM_BANKS][384][8][8];
    uint8_t dirty[GBC_VRAM_BANKS][384]; /* one bit per tile row */
    uint8_t io[0x80], oam[160];
    gbc_color_t bg[32], obj[32], dmg[3][4];
    gbc_color_t live_bg[32], live_obj[32];
    uint16_t next_dot, mode3_end;
    uint32_t apu_pending;
    uint8_t rendering, latched;
} gbc_fast_t;
typedef char gbc_fast_workspace_must_fit_64k[(sizeof(gbc_fast_t) <= 65536) ? 1 : -1];
void gbc_fast_rebuild(gbc_t *gbc);
void gbc_fast_flush_audio(gbc_t *gbc);
void gbc_apu_fast_step(gbc_t *gbc, uint32_t cycles);
void gbc_ppu_fast_step(gbc_t *gbc, uint8_t cycles);
void gbc_fast_palette_write(gbc_t *gbc, uint8_t object, uint8_t byte);
GBC_INLINE void gbc_fast_vram_dirty(gbc_t *gbc, uint8_t bank, uint16_t addr)
{
    if (gbc->fast && (addr & 0x1FFF) < 0x1800) {
        addr &= 0x1FFF;
        gbc->fast->dirty[bank][addr >> 4] |= (uint8_t)(1U << ((addr >> 1) & 7));
    }
}
#endif
