/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "gbc.h"

void gbc_timer_reset(gbc_t *gbc)
{
    gbc->timer.div_counter = 0;
    gbc->timer.tima_counter = 0;
    gbc->timer.tima_reload_delay = 0;
}

static uint8_t gbc_timer_input(gbc_t *gbc, uint8_t tac)
{
    static const uint8_t bits[4] = {9, 3, 5, 7};

    if ((tac & 0x04) == 0) {
        return 0;
    }
    return (uint8_t)((gbc->timer.div_counter >> bits[tac & 0x03]) & 0x01);
}

static void gbc_timer_increment_tima(gbc_t *gbc)
{
    if (gbc->timer.tima_reload_delay != 0) {
        return;
    }
    if (gbc->mmu.io[0x05] == 0xFF) {
        gbc->mmu.io[0x05] = 0x00;
        gbc->timer.tima_reload_delay = 4;
    } else {
        gbc->mmu.io[0x05]++;
    }
}

static void gbc_timer_advance_reload(gbc_t *gbc, uint8_t cycles)
{
    if (gbc->timer.tima_reload_delay == 0) {
        return;
    }
    if (cycles < gbc->timer.tima_reload_delay) {
        gbc->timer.tima_reload_delay = (uint8_t)(gbc->timer.tima_reload_delay - cycles);
        return;
    }
    gbc->timer.tima_reload_delay = 0;
    gbc->mmu.io[0x05] = gbc->mmu.io[0x06];
    gbc_mmu_request_interrupt(gbc, GBC_IF_TIMER);
}

void gbc_timer_write_div(gbc_t *gbc)
{
    uint8_t old_input = gbc_timer_input(gbc, gbc->mmu.io[0x07]);

    gbc->timer.div_counter = 0;
    gbc->mmu.io[0x04] = 0;
    if (old_input) {
        gbc_timer_increment_tima(gbc);
    }
}

void gbc_timer_write_tac(gbc_t *gbc, uint8_t data)
{
    uint8_t old_input = gbc_timer_input(gbc, gbc->mmu.io[0x07]);

    gbc->mmu.io[0x07] = data & 0x07;
    if (old_input && !gbc_timer_input(gbc, gbc->mmu.io[0x07])) {
        gbc_timer_increment_tima(gbc);
    }
}

void gbc_timer_step(gbc_t *gbc, uint8_t cycles)
{
    if (cycles == 0) return;
    uint8_t tac = gbc->mmu.io[0x07];

    gbc_timer_advance_reload(gbc, cycles);

    /* No CPU/MMIO access interleaves these clocks. Count falling edges of
     * the selected DIV bit instead of executing one loop per clock. Keep
     * reload advancement at the instruction boundary, as in the reference. */
    if (tac & 0x04) {
        static const uint8_t shifts[4] = {10, 4, 6, 8};
        uint8_t shift = shifts[tac & 3];
        uint32_t before = gbc->timer.div_counter;
        unsigned edges = ((before + cycles) >> shift) - (before >> shift);
        while (edges-- != 0) gbc_timer_increment_tima(gbc);
    }
    gbc->timer.div_counter = (uint16_t)(gbc->timer.div_counter + cycles);
    gbc->mmu.io[0x04] = (uint8_t)(gbc->timer.div_counter >> 8);
}

