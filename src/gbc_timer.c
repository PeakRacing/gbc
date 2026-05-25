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
}

void gbc_timer_write_div(gbc_t *gbc)
{
    gbc->timer.div_counter = 0;
    gbc->mmu.io[0x04] = 0;
}

void gbc_timer_step(gbc_t *gbc, uint8_t cycles)
{
    static const uint16_t periods[4] = {1024, 16, 64, 256};
    uint8_t tac;
    uint16_t period;

    gbc->timer.div_counter = (uint16_t)(gbc->timer.div_counter + cycles);
    while (gbc->timer.div_counter >= 256) {
        gbc->timer.div_counter = (uint16_t)(gbc->timer.div_counter - 256);
        gbc->mmu.io[0x04]++;
    }

    tac = gbc->mmu.io[0x07];
    if ((tac & 0x04) == 0) {
        return;
    }

    period = periods[tac & 0x03];
    gbc->timer.tima_counter = (uint16_t)(gbc->timer.tima_counter + cycles);
    while (gbc->timer.tima_counter >= period) {
        gbc->timer.tima_counter = (uint16_t)(gbc->timer.tima_counter - period);
        if (gbc->mmu.io[0x05] == 0xFF) {
            gbc->mmu.io[0x05] = gbc->mmu.io[0x06];
            gbc_mmu_request_interrupt(gbc, GBC_IF_TIMER);
        } else {
            gbc->mmu.io[0x05]++;
        }
    }
}

