# gbc

A GB/GBC emulator targeting low-memory MCUs. Project structure, xmake build style, and trimming configuration follow the same conventions as `nes_github`.

## Features

- Supports both GB (DMG) and GBC (CGB) cartridge modes.
- No Boot ROM required; CPU/registers are initialized to post-boot defaults based on cartridge type.
- Core written in C11; `src/` has zero dependency on SDL or OS window interfaces.
- Platform capabilities are provided through weak `port/` functions: memory, filesystem, logging, display, audio, and time.
- Audio disabled by default; four-channel stereo APU available via `GBC_ENABLE_SOUND=1` using integer/fixed-point math only.
- Supported cartridge controllers: ROM ONLY, MBC1, MBC2, MBC3 (with RTC), MBC5.
- Battery-backed SRAM save/load to `.sav` files (configurable via `GBC_ENABLE_SRAM_SAVE`).
- Full-screen framebuffer or half-screen/block refresh modes; full ROM or streamed ROM with LRU bank cache.

## Directory Layout

```text
inc/                Public API and core module headers
src/                Platform-independent emulator core
port/               Default configuration and weak platform functions
sdl/sdl2/           SDL2 desktop verification port and xmake build
tests/              Module regression suites, headless ROM smoke harness and their port configs
AGENTS.md           Chinese contributor/agent guidelines
README_zh.md        Chinese README
```

## Build (SDL2 example)

```powershell
Set-Location .\sdl\sdl2
xmake
xmake run gbc path\to\game.gb
xmake run gbc path\to\game.gbc

# Regression tests
xmake build core_test && xmake run core_test
xmake build sound_test && xmake run sound_test

# Headless smoke test against a real cartridge image
xmake build rom_test && xmake run rom_test path\to\game.gb 600
xmake build rom_test_stream && xmake run rom_test_stream path\to\game.gb 600
```

`xmake run rom_test <rom file> [frames] [audio file] [cycle budget]` boots a real
cartridge headlessly, emulates the requested number of frames (default 60) and
prints the header it parsed, the final CPU register state, a framebuffer
fingerprint and the number of APU bytes produced. It exits non-zero when the
image cannot be loaded or the cartridge controller is unsupported, which makes
it usable as a batch check over a ROM library. The optional cycle budget
(default two million cycles per frame) guards against cartridges that switch the
LCD off: those never produce a frame, so without the budget the harness would
spin forever instead of reporting the stall. `rom_test_stream` builds the same harness with
`GBC_ROM_STREAM=1`, so running both over one image compares the full-ROM and the
LRU bank-cached paths at the final frame (identical hashes and register state mean
the cached path reproduces the resident one). The harness never writes battery
saves, so it is safe to point at a read-only ROM directory.

`sdl/sdl2/xmake.lua` fetches `libsdl2` automatically and compiles `src/**.c`, `sdl/sdl2/main.c`, and the SDL port.

Default key bindings: `W/A/S/D` or arrow keys for D-pad, `J` = A, `K` = B, `V` = Select, `B` = Start, `Esc` = quit.

## Core API

```c
#include "gbc.h"

gbc_t *gbc = gbc_init();
gbc_set_model(gbc, GBC_MODEL_CGB); /* optional: force DMG with GBC_MODEL_DMG */
gbc_load_file(gbc, "game.gbc");    /* loads ROM and battery RAM if present */
gbc_run(gbc);
gbc_unload_file(gbc);              /* saves battery RAM before unloading */
gbc_deinit(gbc);
```

Loading ROM from memory:

```c
gbc_load_rom(gbc, rom_data, rom_size);
```

## Configuration Macros

All macros are set in `port/gbc_conf.h`. Defaults are tuned for PC/SDL debugging; MCU builds override as needed.

| Macro | Default | Description |
| --- | --- | --- |
| `GBC_ENABLE_DMG` | `1` | Enable GB/DMG cartridge support |
| `GBC_ENABLE_CGB` | `1` | Enable GBC/CGB cartridge support |
| `GBC_ENABLE_SOUND` | `0` | Audio output; disabled by default. Enable for S16LE stereo PCM |
| `GBC_APU_SAMPLE_RATE` | `32000` | APU sample rate (fixed-point accumulator, no floating point) |
| `GBC_APU_BUFFER_SAMPLES` | `256` | Interleaved S16LE stereo frames per audio flush |
| `GBC_RAM_LACK` | `0` | `1`: use half-screen/block refresh to reduce framebuffer RAM |
| `GBC_FRAME_SKIP` | `0` | Number of rendered frames to skip per emulated frame |
| `GBC_COLOR_DEPTH` | `32` | `16` = RGB565, `32` = ARGB8888 |
| `GBC_COLOR_SWAP` | `0` | Swap RGB565 bytes for SPI panels |
| `GBC_USE_FS` | `1` | Enable filesystem-based ROM loading |
| `GBC_ENABLE_SRAM_SAVE` | `(GBC_USE_FS)` | Save battery-backed RAM to `.sav` file; auto-disabled when `GBC_USE_FS=0` |
| `GBC_ENABLE_RTC_SAVE` | `(GBC_ENABLE_SRAM_SAVE)` | MBC3 `.rtc` sidecar; offline time is paused |
| `GBC_ENABLE_SAVE_STATE` | `0` | Versioned file snapshots; requires filesystem |
| `GBC_ENABLE_BOOT_ROM` | `0` | Borrow an externally supplied DMG/CGB boot image |
| `GBC_ROM_STREAM` | `0` | `1`: stream ROM banks from file with LRU bank cache |
| `GBC_ROM_CACHE_BANKS` | `4` | Number of 16 KiB ROM cache banks for streaming mode |
| `GBC_LOG_LEVEL` | `GBC_LOG_LEVEL_INFO` | Log verbosity |

Recommended low-memory MCU configuration:

```c
#define GBC_COLOR_DEPTH      (16)
#define GBC_RAM_LACK         (1)
#define GBC_ROM_STREAM       (1)
#define GBC_ROM_CACHE_BANKS  (3)
#define GBC_ENABLE_SOUND     (0)
#define GBC_ENABLE_SRAM_SAVE (0)   /* disable if no persistent storage */
```

## Porting

Replace or implement the following functions in your platform port:

- `gbc_malloc` / `gbc_free`
- `gbc_memcpy` / `gbc_memset` / `gbc_memcmp`
- `gbc_fopen` / `gbc_fread` / `gbc_fwrite` / `gbc_fseek` / `gbc_ftell` / `gbc_fclose`
- `gbc_draw` — receives pixel buffer for the current scanline block
- `gbc_sound_output` — receives S16LE stereo PCM bytes when `GBC_ENABLE_SOUND=1`
- `gbc_log_printf`
- `gbc_get_ticks` / `gbc_delay`
- `gbc_initex` / `gbc_deinitex` / `gbc_frame` — optional lifecycle hooks
- File persistence: `gbc_file_sync`, `gbc_file_replace`, `gbc_file_remove`, `gbc_file_exists`; replacement must preserve the old file on failure.
- Save-state ports: `gbc_state_loaded` clears queued audio and presentation pacing.

SDL accepts `--boot-rom image.bin`; without it startup remains immediate. F6 saves
battery RAM/RTC, F5 saves one snapshot slot, F9 restores it. Exit save failure
offers retry, continued play, or explicit discard. Core file/Flash APIs and error
handling are documented in [storage](docs/storage.md), [Boot ROM](docs/boot_rom.md),
and [validation/resource results](docs/storage_validation.md).

## Known Limitations

This is an MCU-focused MVP, not a cycle-accurate emulator:

- SM83 CPU covers all common opcode paths; a few edge-case timing behaviors may not be exact.
- PPU uses scanline rendering; Mode 3 length is approximated by `SCX&7` + visible sprite count × 6. Pixel FIFO border behavior is not fully accurate.
- APU is off by default; when enabled, four-channel approximate stereo (square, wave, noise) is provided using integer/fixed-point arithmetic only.
- MBC3 RTC, HBlank and general-purpose HDMA, basic OAM DMA blocking, and basic internal-clock serial transfer are implemented; infrared and link-cable serial are not. A general-purpose HDMA transfer holds the CPU off the bus while it runs, one block per 32 CPU T-cycles (64 in double-speed mode).
- Supported MBC types: ROM ONLY, MBC1, MBC2, MBC3, MBC5. Other types return an unsupported error.

### Low-memory MCU integration

Use `gbc_init_static()` / `gbc_deinit_static()` with caller-owned `gbc_t` storage.
Existing `gbc_load_rom()` already borrows the ROM buffer without copying: retain
that buffer until unload (Flash/XIP is suitable). Do not pass static storage to
`gbc_deinit()`, which frees its context.

`GBC_FRAMEBUFFER_ENABLE=0` draws each scanline from `ppu.line`, removing the full
or half framebuffer. The draw hook must consume/copy pixels synchronously.
`GBC_ENABLE_CGB=0` reduces WRAM/VRAM to 8 KiB each. `GBC_USE_SRAM=0` skips cartridge
RAM allocation and disables RAM/RTC register access; games needing those features
may fail. Use `GBC_ENABLE_SRAM_SAVE=0` to disable disk persistence alone.
Bundled PC/MCU configurations now allow compiler `-D` overrides.

Run `xmake build low_memory_test && xmake run low_memory_test` in `sdl/sdl2` to
verify RGB565, scanline output, DMG bank bounds, borrowed ROM lifetime and zero
context/cartridge/hot-path allocations with filesystem, audio and SRAM disabled.
This host test does not establish MCU frame rate or stack requirements.


### Core performance regressions

The JR operand fetch fix and exact timer/PPU/APU fast paths were reviewed and
transferred from the F1C200S port. `python tests/performance_regression.py`
compares against immutable commit `8619557`: all DIV/TAC values, scanline mode
boundaries with dynamic OAM, and two million APU steps including every PCM byte.
It requires that Git history and GCC are available; use `--cc` to select GCC.
Sound-enabled builds now emit silent PCM at the configured cadence with NR52
off while channel clocks remain frozen. No new configuration or save format is
introduced. Board-only scaling, profiling hooks and compiler LTO flags stay in
the firmware repository. See [review and measured limits](docs/performance_review.md).

`GBC_HALT_BATCH_CYCLES` defaults to 4; MCU ports may opt into bounded idle
HALT aggregation (for example 64). `gbc_step` then returns the accumulated
CPU clocks while retaining 4-clock peripheral/PCM granularity. See
[performance review](docs/performance_review.md) for checks and board results.
Run `python tests/performance_regression.py --render-matrix` for the expanded
renderer and idle HALT differential matrix.
