/* Copyright PeakRacing; SPDX-License-Identifier: Apache-2.0 */
#include "gbc_fast.h"

/* Nominal 0.122 ms at the base clock, with whole-instruction overshoot.
 * MMIO and run-call boundaries settle earlier; large budgets stay bounded. */
#define GBC_FAST_AUDIO_BATCH_DOTS 512U

int gbc_set_run_mode(gbc_t *gbc, gbc_run_mode_t mode)
{
    if (!gbc) return GBC_ERROR;
    if (gbc->running || (mode != GBC_RUN_REFERENCE && mode != GBC_RUN_FAST)) return GBC_ERROR_UNSUPPORTED;
#if (GBC_ENABLE_FAST_RUN == 1)
    if (mode == GBC_RUN_FAST && !gbc->fast) {
        gbc_fast_t *fast = (gbc_fast_t *)gbc_malloc(sizeof(*fast));
        if (!fast) return GBC_ERROR_ALLOC;
        gbc_memset(fast, 0, sizeof(*fast));
        gbc->fast = fast;
        gbc_fast_rebuild(gbc);
    } else if (mode == GBC_RUN_REFERENCE && gbc->fast) {
        gbc_fast_flush_audio(gbc);
        gbc_free(gbc->fast);
        gbc->fast = NULL;
    }
#else
    if (mode == GBC_RUN_FAST) return GBC_ERROR_UNSUPPORTED;
#endif
    gbc->run_mode = mode;
    return GBC_OK;
}

#if (GBC_ENABLE_FAST_RUN == 1)
void gbc_fast_dmg_palette_write(gbc_t *gbc, uint8_t reg)
{
    static const uint8_t shades[4] = {31, 21, 10, 0};
    if (!gbc->fast) return;
    for (uint8_t c = 0; c < 4; c++) {
        uint8_t value = shades[(gbc->mmu.io[reg] >> (c * 2)) & 3];
        gbc->fast->live_dmg[reg - 0x47][c] = gbc_ppu_color(value, value, value);
    }
}

void gbc_fast_palette_write(gbc_t *gbc, uint8_t object, uint8_t byte)
{
    if (!gbc->fast) return;
    uint8_t index = byte >> 1;
    const uint8_t *palette = object ? gbc->ppu.obj_palette : gbc->ppu.bg_palette;
    uint16_t raw = (uint16_t)(palette[index * 2] | (palette[index * 2 + 1] << 8));
    gbc_color_t *cache = object ? gbc->fast->live_obj : gbc->fast->live_bg;
    cache[index] = gbc_ppu_color(raw & 31, (raw >> 5) & 31, (raw >> 10) & 31);
}

void gbc_fast_rebuild(gbc_t *gbc)
{
    if (!gbc->fast) return;
    gbc_fast_t *fast = gbc->fast;
    gbc_memset(fast->dirty, 0xFF, sizeof(fast->dirty));
    fast->latched = fast->rendering = 0;
    fast->next_dot = 0;
    fast->apu_pending = 0;
    for (uint8_t i = 0; i < 64; i += 2) {
        gbc_fast_palette_write(gbc, 0, i);
        gbc_fast_palette_write(gbc, 1, i);
    }
    for (uint8_t reg = 0x47; reg <= 0x49; reg++) gbc_fast_dmg_palette_write(gbc, reg);
}

void gbc_fast_flush_audio(gbc_t *gbc)
{
    if (!gbc->fast) return;
    uint32_t cycles = gbc->fast->apu_pending;
    gbc->fast->apu_pending = 0;
    gbc_apu_fast_step(gbc, cycles);
}
#endif

#if (GBC_ENABLE_FAST_RUN == 1)
static uint8_t gbc_fast_idle_cycles(gbc_t *gbc, uint32_t remaining)
{
    uint32_t limit = remaining < 252 ? remaining : 252;
    uint32_t event;
    if ((gbc->mmu.io[0x40] & 0x80) && gbc->fast->next_dot > gbc->ppu.dot_cycles) {
        event = (gbc->fast->next_dot - gbc->ppu.dot_cycles) << gbc->mmu.double_speed;
        if (event < limit) limit = event;
    } else if (gbc->mmu.io[0x40] & 0x80) return 4;
    if (gbc->timer.tima_reload_delay && gbc->timer.tima_reload_delay < limit) limit = gbc->timer.tima_reload_delay;
    if (gbc->mmu.io[0x07] & 4) {
        static const uint16_t periods[4] = {1024, 16, 64, 256};
        uint16_t period = periods[gbc->mmu.io[0x07] & 3];
        event = period - (gbc->timer.div_counter & (period - 1));
        if (event < limit) limit = event;
    }
    if (gbc->mmu.serial_cycles && gbc->mmu.serial_cycles < limit) limit = gbc->mmu.serial_cycles;
    if (gbc->mmu.oam_dma_cycles && gbc->mmu.oam_dma_cycles < limit) limit = gbc->mmu.oam_dma_cycles;
    event = (GBC_FAST_AUDIO_BATCH_DOTS - gbc->fast->apu_pending) << gbc->mmu.double_speed;
    if (event < limit) limit = event;
    /* HALT still observes events on its original four-clock boundary. */
    limit = (limit + 3) & ~3UL;
    return (uint8_t)(limit < 4 ? 4 : limit);
}
#endif

uint32_t gbc_run_cycles(gbc_t *gbc, uint32_t budget)
{
    uint32_t total = 0;
    if (!gbc || !budget || gbc->gbc_quit || !gbc->cart.rom_data || gbc->running) return 0;
    /* Leave room for the last whole instruction/idle event's overshoot. */
    if (budget > UINT32_MAX - 252U) budget = UINT32_MAX - 252U;
#if (GBC_ENABLE_FAST_RUN == 1)
    if (gbc->run_mode == GBC_RUN_FAST) {
        gbc->running = 1;
        do {
            uint8_t cycles;
            if (gbc->cpu.halted && !gbc->mmu.hdma_busy_cycles &&
                !(gbc->mmu.ie & gbc->mmu.io[0x0F] & 0x1F)) {
                cycles = gbc_fast_idle_cycles(gbc, budget - total);
                gbc->cpu.cycles += cycles;
            } else cycles = gbc_cpu_step(gbc);
            uint8_t dots = gbc->mmu.double_speed ? (uint8_t)(cycles >> 1) : cycles;
            gbc_mmu_step_dma(gbc, cycles);
            gbc_mmu_step_serial(gbc, cycles);
            gbc_timer_step(gbc, cycles);
            gbc_ppu_fast_step(gbc, dots);
            gbc->fast->apu_pending += dots;
            /* Bound audio/control latency even with the LCD disabled. */
            if (gbc->fast->apu_pending >= GBC_FAST_AUDIO_BATCH_DOTS) gbc_fast_flush_audio(gbc);
            total += cycles;
            if (gbc->ppu.frame_ready) {
                gbc->ppu.frame_ready = 0;
                gbc->frame_count++;
#if (GBC_FRAME_SKIP != 0)
                if (gbc->gbc_frame_skip_count >= GBC_FRAME_SKIP) gbc->gbc_frame_skip_count = 0;
                else gbc->gbc_frame_skip_count++;
#endif
                gbc_frame(gbc);
            }
        } while (!gbc->gbc_quit && total < budget);
        gbc_fast_flush_audio(gbc);
        gbc->running = 0;
        return total;
    }
#endif
    do {
        uint8_t cycles = gbc_step(gbc);
        if (!cycles) break;
        total += cycles;
    } while (!gbc->gbc_quit && total < budget);
    return total;
}
