/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#pragma once
#ifndef GBC_ENABLE_SAVE_STATE
#define GBC_ENABLE_SAVE_STATE (1)
#endif

#ifdef __cplusplus
    extern "C" {
#endif

#ifndef GBC_ENABLE_SOUND
#define GBC_ENABLE_SOUND            (1)       /* four-channel stereo APU; disable for MCU builds */
#endif
#ifndef GBC_APU_SAMPLE_RATE
#define GBC_APU_SAMPLE_RATE         (32000)   /* stereo PCM sample rate when sound is enabled */
#endif
#ifndef GBC_APU_BUFFER_SAMPLES
#define GBC_APU_BUFFER_SAMPLES      (256)     /* interleaved S16LE stereo frames per output batch */
#endif
#ifndef GBC_ENABLE_DMG
#define GBC_ENABLE_DMG              (1)       /* support original GB/DMG cartridges */
#endif
#ifndef GBC_ENABLE_CGB
#define GBC_ENABLE_CGB              (1)       /* support GBC/CGB cartridges */
#endif

#ifndef GBC_FRAME_SKIP
#define GBC_FRAME_SKIP              (0)       /* skip rendered frames */
#endif
#ifndef GBC_COLOR_DEPTH
#define GBC_COLOR_DEPTH             (32)      /* 16: RGB565, 32: ARGB8888 */
#endif
#ifndef GBC_COLOR_SWAP
#define GBC_COLOR_SWAP              (0)       /* swap RGB565 bytes for SPI panels */
#endif
#ifndef GBC_RAM_LACK
#define GBC_RAM_LACK                (0)       /* 1: draw half-screen blocks to reduce RAM */
#endif

#ifndef GBC_USE_FS
#define GBC_USE_FS                  (1)       /* enable file based ROM loading */
#endif
#ifndef GBC_ENABLE_SRAM_SAVE
#define GBC_ENABLE_SRAM_SAVE        (GBC_USE_FS) /* save battery RAM when filesystem is available */
#endif
#ifndef GBC_ROM_STREAM
#define GBC_ROM_STREAM              (0)       /* 1: stream ROM banks from file with LRU cache */
#endif
#ifndef GBC_ROM_CACHE_BANKS
#define GBC_ROM_CACHE_BANKS         (4)       /* 16KiB cache banks used when GBC_ROM_STREAM=1 */
#endif

#ifndef GBC_LOG_LEVEL
#define GBC_LOG_LEVEL               GBC_LOG_LEVEL_INFO
#endif

#ifdef __cplusplus
    }
#endif

