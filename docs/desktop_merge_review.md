# Desktop snapshot comparison and merge review

Reviewed on 2026-10-11. Destination: `F:\code\codeup\gbc`, branch `main`,
base commit `220f79b`. Reference snapshot: `C:\Users\qq147\Desktop\gbc`.
The destination already contained uncommitted core, configuration, documentation
and regression-test changes. Those were reviewed along with the reference;
a pre-edit backup was saved outside the repository at
`C:\Users\qq147\Desktop\gbc-merge-backup-20261011`.

## Comparison decisions

The snapshot uses a mapper vtable and an inline CPU bus. The main repository
uses `gbc_cart_t` and an independent MMU, including filesystem ROM streaming
and an LRU bank cache. Whole-file replacement would discard working functionality
and break the main public API, so changes were adapted by behavior.

| Desktop behavior | Main repository result |
|---|---|
| Static emulator lifecycle | Add `gbc_init_static` / `gbc_deinit_static` alongside the existing allocating API |
| Caller-owned ROM / Flash mapping | Existing `gbc_load_rom` already borrows ROM; document its lifetime rather than introduce a redundant API |
| File loading without a second ROM copy | Already implemented; retain resident and streaming paths |
| Optional scanline-only output | Add `GBC_FRAMEBUFFER_ENABLE=0`; render directly from existing `ppu.line` |
| DMG memory reduction | `GBC_ENABLE_CGB=0` compiles WRAM/VRAM down to 8 KiB each; bound DMG bank writes |
| Disabled cartridge RAM allocation | Add `GBC_USE_SRAM=0`; skip allocation and RAM/RTC register access, and suppress battery persistence |
| Configuration overrides | Wrap bundled port defines in `#ifndef` |
| Additional mapper files / SDL3 | Not imported: preserve the main architecture and explicit unsupported-controller errors; desktop MBC6/MBC7 implementations do not establish working compatibility |
| CPU/timer/PPU/APU source | Retain main implementation; port only relevant behavior and repair reviewed correctness faults |

## Review corrections

- The pre-existing EI change enabled IME before the following instruction. Keep
  the delay through that instruction, let DI cancel it, and prevent consecutive
  EI instructions from extending the delay. RETI clears delayed IME.
- HALT must wake when an enabled interrupt becomes pending even if IME is clear;
  waking and servicing are separate behaviors.
- Select the first ten overlapping OAM entries. Sort those entries by X only in
  DMG mode; CGB drawing uses OAM order. Use ten stack slots, rather than collecting
  and sorting forty objects on every line.
- OBJ and BG have separate 64-byte palette RAM arrays. The pre-existing OBJ
  palette offset would corrupt palette selection and read past palette 7.
  Restore direct OBJ palette indexing and test palette 7 and overlapping objects.
- General-purpose VRAM DMA stalls opcode fetch and interrupt service. A block
  takes 32 CPU T-cycles normally, 64 at double speed, rather than 128.
  Count concurrent OAM DMA using the original elapsed CPU cycles.
- In double-speed mode DIV/TIMA, serial and OAM DMA follow CPU cycles, while
  PPU/APU follow half that count.
- Transfer ROM allocation ownership only after cartridge loading succeeds.
  Previously, a cartridge RAM allocation failure could free a file ROM in the
  loader and then free it again in its caller.
- Remove SDL linking from headless targets. Correct documentation that described
  a final-state comparison as a frame-by-frame comparison.

Hardware references: [interrupts](https://gbdev.io/pandocs/Interrupts.html),
[OAM selection and priority](https://gbdev.io/pandocs/OAM.html),
[CGB clocks and DMA](https://gbdev.io/pandocs/CGB_Registers.html).

## Validation

MSVC C11 release builds passed for `gbc`, `core_test`, `sound_test`,
`low_memory_test`, `rom_test` and `rom_test_stream` in `sdl/sdl2`.
Core and APU regressions passed, including allocation-failure ownership,
EI/DI/consecutive-EI, HALT wakeup, palette 7, CGB priority, normal/fast DMA
and double-speed timer/PPU tests.

The low-memory test uses RGB565, DMG only, no framebuffer, filesystem, SRAM or
audio. On Windows x64 it reports `sizeof(gbc_t)=18432`, WRAM/VRAM 8192 bytes each,
and a 320-byte pixel line. Static initialization, borrowed ROM loading, rendering
144 lines and deinitialization use zero heap allocations. This does not include
the caller's ROM backing storage, platform resources or stack, and does not
establish the ABI-dependent context size on an actual MCU. Other configurations
can allocate cartridge RAM and resident ROM memory.

Three external ROMs were each run for 600 emulated frames using both resident
and streaming builds. All six runs exited successfully. Final CPU registers,
LCD state, framebuffer fingerprints and audio byte counts matched between modes.
These builds disable battery persistence and do not write to the ROM directory.

| ROM | Controller | Final framebuffer hash | Audio bytes |
|---|---|---|---|
| Super Mario Bros. Deluxe | MBC5 | `488EC2F5` | 1328128 |
| Pocket Monsters Crystal (Japan) | MBC3 + RTC | `1C82EF96` | 1312768 |
| Dragon Quest Monsters Terry (Japan, Rev 1) | MBC1 | `19AF96DC` | 1382400 |

These are bounded startup/frame-production tests, not gameplay, audio-quality,
save compatibility or real-device acceptance. No ROMs or build outputs are
included in the commits. Pixel FIFO precision, hardware DMA interactions and
unimplemented controller types remain limitations of the existing core.
