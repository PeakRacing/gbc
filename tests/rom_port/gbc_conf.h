/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

/*
 * Port configuration for the headless real ROM smoke test (tests/rom_smoke_test.c).
 *
 * The harness boots genuine cartridge images that live outside the repository,
 * so it must never write a battery backed save next to them: GBC_ENABLE_SRAM_SAVE
 * is forced off even though file access itself is enabled.
 */

#pragma once

#define GBC_ENABLE_SOUND            (1)
#define GBC_ENABLE_DMG              (1)
#define GBC_ENABLE_CGB              (1)
#define GBC_FRAME_SKIP              (0)
#define GBC_COLOR_DEPTH             (32)
#define GBC_COLOR_SWAP              (0)
#define GBC_RAM_LACK                (0)
#define GBC_USE_FS                  (1)
#define GBC_ENABLE_SRAM_SAVE        (0)
#define GBC_ROM_STREAM              (0)
#define GBC_ROM_CACHE_BANKS         (4)
#define GBC_LOG_LEVEL               GBC_LOG_LEVEL_NONE
