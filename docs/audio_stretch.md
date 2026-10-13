# Optional integer PCM time stretching

`port/gbc_audio_stretch.h` is a frontend helper, independent of the emulator
core and disabled unless a port explicitly uses it. It was imported from the
F1C200S handheld's qualified audio candidate on 2026-10-13. Only the public
identifier prefix and C++ linkage wrapper were changed. Source header SHA256:
`8d2a739116ac522159d3fbfd6cf8db3497fea9e91bda0db476307ef0be56d7be`.

The fixed-point WSOLA overlap search uses one splice for both stereo channels.
At full speed it copies successive PCM blocks without searching. It changes
neither CPU/APU execution nor save data. It is not an emulator speedup.

## Caller contract

- Input is aligned, interleaved 32 kHz S16 stereo PCM. Convert bytes to stereo
  frames with `bytes / 4`; reject incomplete frames before calling.
- Allocate one `struct gbc_audio_stretch` per stream (9528 bytes), call
  `gbc_audio_stretch_reset` before feeding, and serialize all access.
- `gbc_audio_stretch_feed` returns 0 or -1 if the ring cannot accept the entire
  batch. Failure does not consume samples. Feed small batches (256 frames),
  drain available output after each feed, and handle failure explicitly.
- `gbc_audio_stretch_render` returns 320 stereo frames or 0 when more input is
  needed. Its output buffer must hold 640 `int16_t` values. Input/output buffers
  must not alias the state. Nonempty buffers and state pointers must be valid.
- Q16 is source time divided by output time, clamped to [32768, 65536]. At
  0.87, it produces approximately 1/0.87 times as many PCM frames. Stereo
  pitch is approximately preserved; playback tempo follows the slower game.
- Reset on stream restart/state load. Buffered PCM is derived state and must
  not be serialized. There is no end-of-stream flush: finite recordings retain
  lookahead, including a 960-frame startup threshold. This helper targets live
  playback, not lossless finite-file conversion.

Repack 320-frame outputs into the existing 256-frame device batches. Advance
the pointer by *accepted* stereo frames on partial writes; retain the remaining
packet, enforce timeout/stop handling, and never silently discard feed errors.
The helper performs no allocation, I/O, queue pacing or producer-rate estimation.

The handheld estimates available production speed over 250 ms windows, excluding
only intentional queue-pacing sleeps, and immediately updates the clamped ratio.
It reserves about 40 ms of output audio. Ports must measure their own producer
and device clocks; deriving this ratio from presented FPS or counting deliberate
pacing as compute time can cause persistent unnecessary slowing. Provide a raw
fallback. The qualified handheld selects stretching for FAST mode, raw for
REFERENCE mode; no default core/SDL policy is changed here.

## Review and validation

Reviewed ring retention/lookahead, full S16 arithmetic bounds, shared stereo
splice, rate transitions and allocation lifetime. The squared error divides
differences by eight before squaring; mixing fits signed 32-bit arithmetic.
Synthetic tests cover counts, 50–2000 Hz pitch, amplitude bounds, full-speed
byte identity, ring wraps, changing ratios and stereo channel-swap equivalence.

Run from repository root:

```powershell
python tools/test_audio_stretch.py --cc D:/msys64/mingw64/bin/gcc.exe --output .xmake/audio-stretch-review
```

For import equivalence checking, optionally append `--reference-header` followed
by the original handheld header path. Synthetic outputs are compared byte for
byte at all four ratios and across rate transitions.

No copyrighted ROM or recorded game audio is required. These host tests verify
the helper, not DAC behavior, queue underruns or subjective sound quality.

Previously qualified F1C200S firmware, three alternating 60-second windows:

| Mario Deluxe dual-player demo median | Raw | Adapted |
| --- | ---: | ---: |
| Simulated FPS | 52.146 | 51.664 |
| Underrun silence per minute | 7445.125 ms | 70 ms |
| Underrun count | 2909 | 17 |

The simulated-rate cost was 0.925%; silence decreased by 99.06%. All frames were
drawn. GB and the representative MBC3 CGB sample remained around 59.4–59.5 FPS.
The user confirmed a clear audible improvement for the RAM candidate. The final
firmware's full NOR readback and normal startup were separately verified; this
repository import is host validated and is not a new board deployment.

Residual limits: Mario remains below 59.73 simulated FPS; this trades extra
buffering and approximate waveforms for continuity. It cannot mask indefinite
operation below half speed, and does not guarantee zero underruns or artifacts.
Rejected DC removal and underrun-edge fades are not part of this module.
