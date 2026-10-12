# Cartridge persistence

File-backed loading binds the ROM filename as the persistence base path. For
borrowed Flash/XIP ROMs, bind a filesystem path using `gbc_persist_set_path()`
and explicitly call `gbc_persist_load()` before running. A NULL base detaches
persistence. `GBC_USE_SRAM=0` or disabled filesystem support removes persistence.

Raw `.sav` files retain the legacy RAM layout (512 bytes for MBC2). Writes mark
RAM dirty only when a byte changes. `gbc_persist_save()` and normal unload save
dirty RAM; there is no periodic or per-frame save. The platform synchronizes a
temporary sibling file, closes it, then replaces the destination without deleting
the old file first. Ports must implement sync/replace/remove/exists hooks when
storage is enabled. Atomic replacement and power-loss durability depend on the
platform/filesystem; the supplied desktop implementations synchronize file data.

Unload/deinit failures retain the context and cartridge so the caller can retry.
Use `gbc_unload_rom_discard()` to explicitly discard changes, then deinitialize.
Malformed or unreadable sidecars return an error and block subsequent saves,
preventing silent destruction of the original. A failed file-backed load can
retain its cartridge for inspection; discard it before retrying a different ROM.
An inaccessible file is an error, whereas a missing save starts a new game.

Regression command: `xmake run feature_test` in `sdl/sdl2`. This exercises MBC1,
MBC2, explicit borrowed-ROM paths, short reads, short writes, sync/close/replace
failure, retained cartridges and unchanged old save data. Existing `core_test`
also checks file-backed round trips. No copyrighted ROM images are included.

## RTC

`GBC_ENABLE_RTC_SAVE` defaults to `GBC_ENABLE_SRAM_SAVE` and can be disabled
independently. MBC3 timer cartridges use a separate 32-byte `.rtc` sidecar:
`GRTC` magic, version 1, live/latched registers, latch edge state, millisecond
remainder and CRC32, all integers encoded little-endian. No process timestamp
or wall-clock timestamp is stored. Restore establishes a fresh tick baseline,
so time while the emulator was closed never advances the cartridge clock.
Running time, halt, day carry and latched values are retained.

Each `.sav` and `.rtc` is committed independently; their pair is not an atomic
transaction. Either failure is reported. A bad RTC file resets RTC data and
blocks saving until explicitly detached or successfully reloaded. Regression
tests cover day rollover, halt/carry, subsecond remainder, latched values,
elapsed offline time, failed replacement and corrupted sidecars.

## 即时存档

核心默认 GBC_ENABLE_SAVE_STATE=0，需要文件系统。在指令边界调用
`gbc_state_save_file()` / `gbc_state_load_file()`，返回错误码。
GSTA v1 使用小端标量、分块和 CRC32，验证 ROM、配置与字段范围。
排除指针、句柄、ROM、LRU 和可重绘画面；I/O 缓冲区为 256 字节，
首次 ROM 校验另有 256 字节局部缓冲区，不分配完整状态副本。
加载先验证再应用；验证失败保持状态，应用失败设置 state_fault/quit，
必须放弃卸载并重新加载 ROM。恢复重建缓存、RTC 基准并通知平台清空音频。
SDL：F5 保存 .state、F9 恢复、F6 保存 RAM/RTC；退出失败可重试或放弃。
三款外部 ROM 的常驻/流式各运行180帧，恢复重放60帧的 CPU、MMU、画面、
音频哈希均一致。此为有界主机验证，不是 MCU 或长期游戏存档验收。
