/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "gbc.h"
#include "../port/gbc_fs_impl.h"

#if (GBC_ENABLE_SAVE_STATE == 1) && (GBC_USE_FS == 1)
GBC_WEAK void gbc_state_loaded(gbc_t *gbc)
{
    (void)gbc;
}
#endif
