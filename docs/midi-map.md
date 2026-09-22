# Control protocol (MIDI)

One protocol for every control path: physical controls on the Daisy, BLE-MIDI,
the web UI (JSON ⇄ MIDI on the ESP32) and the emulator's MIDI input all speak this map.
Defined in `firmware/dsp/include/pedal/midi.h`; the web UI's JSON form is in `firmware/esp32/src/main.cpp`.

Effects: 0 Tremolo, 1 Delay, 2 Reverb, 3 Fuzz. Parameters are normalised 0..1; the
real-unit curves are in each effect's `paramDesc()` (and mirrored in `web/index.html`).

| message | meaning |
|---|---|
| CC 0–31 (MSB), CC 32–63 (LSB) | effect `cc/8`, parameter `cc%8`; 14-bit when the LSB follows |
| CC 64–67 | enable effect 0–3 (≥64 = on) |
| CC 70 / 71 | global high-cut / low-cut toggle position 0 off, 1 slot A, 2 slot B |
| CC 72 | tap tempo (each message = one tap) |
| CC 74 / 75 | input / output trim, −12…+12 dB |
| PC n | load preset n (0–7) |
| SysEx `F0 7D 01 o0 o1 o2 o3 F7` | chain order (effect ids in signal order) |
| SysEx `F0 7D 02 n F7` / `03 n` | save / load preset n |
| SysEx `F0 7D 04 s hi lo F7` | high-cut slot s (0/1) corner frequency, 14-bit Hz |
| SysEx `F0 7D 05 s hi lo F7` | low-cut slot s corner frequency |
| SysEx `F0 7D 06 hi lo F7` | tempo, 14-bit ms per beat |
| SysEx `F0 7D 07 F7` | request full state → device answers with a dump (all of the above) |
| SysEx `F0 7D 20 F7` | Daisy → ESP32: start the WiFi AP (TAP held 3 s) |

Parameter index per effect (CC = effect×8 + index):

| | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| Tremolo | Rate | Depth | Shape | Harmonic | StereoPhase | Crossover | Volume | – |
| Delay | Time | Feedback | Mix | LowCut | HighCut | Mod | PingPong | Subdiv |
| Reverb | Mix | Decay | PreDelay | HighCut | LowCut | Mod | Diffusion | Width |
| Fuzz | Gain | Bias | Tone | Volume | Mode | Oversample | Gate | LowCut |

Footswitch gestures (Daisy and emulator): press = toggle effect / tap; hold TAP 0.5 s then
press an effect switch = bind the knob bank to that effect; hold fx1 + fx4 2 s = DFU;
hold TAP 3 s = start WiFi AP.
