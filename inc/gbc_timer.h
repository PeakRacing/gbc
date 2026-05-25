/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#pragma once

#include "gbc_default.h"

#ifdef __cplusplus
    extern "C" {
#endif

typedef struct {
    uint16_t div_counter;
    uint16_t tima_counter;
} gbc_timer_t;

struct gbc;

void gbc_timer_reset(struct gbc *gbc);
void gbc_timer_step(struct gbc *gbc, uint8_t cycles);
void gbc_timer_write_div(struct gbc *gbc);

#ifdef __cplusplus
    }
#endif

