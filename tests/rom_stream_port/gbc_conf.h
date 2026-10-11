/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

/*
 * Port configuration for the streamed ROM smoke build (rom_test_stream).
 *
 * Identical to tests/rom_port/gbc_conf.h except that the cartridge image is
 * streamed from the file through the LRU bank cache instead of being held in
 * RAM, so the same harness output can be compared between the two modes.
 * As in tests/rom_port, battery saves stay off: these runs boot real cartridges
 * that live outside the repository.
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
#define GBC_ROM_STREAM              (1)
#define GBC_ROM_CACHE_BANKS         (3)
#define GBC_LOG_LEVEL               GBC_LOG_LEVEL_NONE
