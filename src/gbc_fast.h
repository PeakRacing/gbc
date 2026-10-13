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
    gbc_color_t live_dmg[3][4];
    uint16_t next_dot, mode3_end;
    uint32_t apu_pending;
    uint8_t rendering, latched;
    uint8_t sprites[10], sprite_count; /* Mode-2 selection in OAM order */
} gbc_fast_t;
typedef char gbc_fast_workspace_must_fit_64k[(sizeof(gbc_fast_t) <= 65536) ? 1 : -1];
/* Nominal 0.122 ms audio settlement, plus whole-instruction overshoot. */
#define GBC_FAST_AUDIO_BATCH_DOTS 512U
uint32_t gbc_cpu_fast_run(gbc_t *gbc, uint32_t budget);
void gbc_fast_rebuild(gbc_t *gbc);
void gbc_fast_flush_audio(gbc_t *gbc);
void gbc_apu_fast_step(gbc_t *gbc, uint32_t cycles);
void gbc_ppu_fast_step(gbc_t *gbc, uint8_t cycles);
/* With TAC disabled and no pending reload, DIV is the only changing state.
 * MMIO writes still use the original edge/reload handlers. */
GBC_INLINE void gbc_fast_timer_advance(gbc_t *gbc, uint8_t cycles)
{
    if (!cycles) return;
    if ((gbc->mmu.io[0x07] & 4) || gbc->timer.tima_reload_delay) {
        gbc_timer_step(gbc, cycles);
    } else {
        gbc->timer.div_counter = (uint16_t)(gbc->timer.div_counter + cycles);
        gbc->mmu.io[0x04] = (uint8_t)(gbc->timer.div_counter >> 8);
    }
}
/* Most CPU instructions cannot reach a PPU event. Keep this small path in
 * the scheduler; mode/LY/IRQ/DMA/render events still use the original handler. */
GBC_INLINE void gbc_fast_ppu_advance(gbc_t *gbc, uint8_t cycles)
{
    gbc_fast_t *fast = gbc->fast;
    gbc_ppu_t *ppu = &gbc->ppu;
    if (gbc->mmu.io[0x40] & 0x80) {
        uint16_t next = (uint16_t)(ppu->dot_cycles + cycles);
        if (fast->next_dot && next < fast->next_dot) {
            ppu->dot_cycles = next;
            return;
        }
    } else if (!ppu->dot_cycles && !ppu->mode && !gbc->mmu.io[0x44] &&
               !fast->next_dot && !fast->latched) {
        return; /* LCD-off state already normalized by the event handler. */
    }
    gbc_ppu_fast_step(gbc, cycles);
}
void gbc_fast_palette_write(gbc_t *gbc, uint8_t object, uint8_t byte);
void gbc_fast_dmg_palette_write(gbc_t *gbc, uint8_t reg);
GBC_INLINE void gbc_fast_vram_dirty(gbc_t *gbc, uint8_t bank, uint16_t addr)
{
    if (gbc->fast && (addr & 0x1FFF) < 0x1800) {
        addr &= 0x1FFF;
        gbc->fast->dirty[bank][addr >> 4] |= (uint8_t)(1U << ((addr >> 1) & 7));
    }
}
#endif
