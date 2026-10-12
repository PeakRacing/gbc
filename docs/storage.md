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
