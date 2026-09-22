# firmware/dsp — portable DSP core

Header-only C++17, no heap in the audio path, no exceptions/RTTI, no libDaisy
dependency. Compiled unchanged into:

- `firmware/daisy/` (STM32H750, arm-none-eabi-gcc, buffers in SDRAM via the `Allocator`)
- `emulator/` (macOS, clang, buffers on the heap)

| header | contents |
|---|---|
| `pedal/core.h` | `Frame`, `Allocator`, `ParamDesc`, `Smoother`, `Biquad`, `Lfo`, `DelayLine` |
| `pedal/effect.h` | `Effect` interface (params, enable with trails, tempo hook) |
| `pedal/tremolo.h` | LFO tremolo, harmonic mode, stereo phase |
| `pedal/delay.h` | 2 s stereo fractional delay, filtered/saturated feedback, wow/flutter, ping-pong, subdivisions |
| `pedal/reverb.h` | Dattorro plate |
| `pedal/oversampler.h` | polyphase FIR 1x/2x/4x |
| `pedal/fuzz.h` | oversampled ADAA waveshaper, tilt tone, gate |
| `pedal/pedal.h` | `Pedal`: chain order, global HP/LP toggles, tap tempo, presets, listener |
| `pedal/midi.h` | MIDI parser + the control map shared with the ESP32 bridge |

Rules: every effect's `process()` is called every block so it can render trails;
state-less effects return immediately when disabled. Parameter changes are
smoothed inside the effects; the caller may set them from any thread as long
as each `setParam` is a single float store.
