# Multi-effect DSP guitar pedal — research findings and design plan

## Context

Max wants a one-off guitar pedal with tremolo, delay, reverb and fuzz, all implemented in DSP, built on a dev board rather than a bare-MCU custom design. Requirements gathered in Q&A:

- All four effects digital (no analog fuzz stage), each with its own footswitch, running simultaneously, with the chain order re-arrangeable.
- Stereo-capable I/O (2× TRS jacks, mono-compatible), buffered bypass through the DSP (no relay), 9 V centre-negative barrel power, no internal supply.
- Wireless control via a web page served by the pedal **and** BLE-MIDI; firmware updates over USB.
- Physical: 4 effect footswitches + tap-tempo footswitch, a shared bank of ~6 knobs with effect-select, two physical toggles for global high-cut / low-cut (corner frequency set in the app), per-effect HP/LP cuts in the app only.
- Budget ≈ $250 for parts; custom carrier PCB is in scope.

The directory `/Users/maxteicheira/Documents/Claude/guitar_pedal` is empty, so implementation starts from zero. This plan records the research and the recommended architecture; the deliverables of the implementation phase are a project README, a block diagram/BOM, KiCad schematic for the carrier board, Daisy firmware, and ESP32 firmware.

---

## 1. Platform decision

**Recommended: Electro-Smith Daisy Seed3 (audio DSP) + ESP32-C3 module (radio co-processor), linked by UART carrying MIDI.**

| Candidate | DSP | Radio | Verdict |
|---|---|---|---|
| **Daisy Seed3** — STM32H750 Cortex-M7 @480 MHz, TI TAC5242 codec, 192 kHz/32-bit, 64 MB SDRAM, 8 MB QSPI, USB-C DFU, $29.99, in stock | Strong; libDaisy + DaisySP; large pedal ecosystem (Terrarium, Hothouse, Funbox, GuitarML) | none | **Pick.** Pin-compatible with all existing Seed pedal designs. |
| Teensy 4.1 + audio shield | Strongest CPU (600 MHz) | none | Viable, but 16-bit SGTL5000 shield and a smaller pedal ecosystem. |
| ESP32-S3 + external codec | Weak for reverb + delay + oversampled fuzz simultaneously; no FPU-heavy DSP headroom | built-in | Reject as sole processor. |
| RP2350 | No hardware FPU parity with M7; small ecosystem for audio | none | Reject. |

Why two chips: the Seed3 has no radio; ESP32 alone can't carry the DSP load. Prior art for exactly this split exists (`TMANTsmith/daisy-esp32-guitar-pedal`, ESP32-C6 + Daisy, web UI on the ESP32).

Why UART not SPI/USB: ESP32-S2/S3 share one USB PHY between USB-host and the radio, so USB between the two boards is a coexistence trap. UART at 115200–921600 baud is plenty for control traffic and the Seed3 has free USARTs (D13/D14 = USART1).

Why ESP32-C3 over S3: the load is a web server + BLE-MIDI + UART bridge, which the single-core C3 handles; it is cheaper and smaller (SuperMini form factor, ~18 mm wide). Use the **S3 or C6** only if a later OLED or heavier UI is wanted. Note: simultaneous WiFi + BLE on ESP32 works but is finicky in Arduino — plan to run the radio via ESP-IDF or Arduino-on-IDF with the coexistence config enabled, and default to **BLE on, WiFi AP started on a long-press** so they rarely fight.

## 2. Block diagram

```
 9V DC barrel (centre −) ──► reverse-polarity + polyfuse + filter (Seed3 datasheet Fig 1.1) ──┬──► +9V_A analog rail (op-amps only)
                                                                                              └──► buck 9V→5V (≥600 mA) ──┬──► LC/RC filter (Fig 1.4) ──► Seed3 VIN
                                                                                                                          └──► ESP32-C3 5V pin ──► its own 3V3 LDO

 IN (TRS) ─► 1 MΩ JFET/OPA1652 buffer ×2 (L/R) ─► level shift + anti-alias RC ─► Seed3 Audio In 1/2
 Seed3 Audio Out 1/2 ─► OPA1652 output stage ×2 (+9 V rail, gain ≈ ×2, 100 Ω out) ─► OUT (TRS)
                        (TRS ring: switch contact on IN jack → GPIO "stereo cable present")

 Seed3 GPIO/ADC:
   ADC0–5  : 6 knobs (B10k to 3V3A)
   D-pins  : 5 momentary SPST footswitches (to GND, internal pull-up)
             2 × ON-OFF-ON toggles (global HP / LP: off / low / high)
             5 LEDs (4 effect state, 1 tempo blink)  — or 4 RGB LEDs so colour = which effect the knob bank edits
   USART1  : ESP32-C3 (3.3 V logic both sides, TX/RX only)
   USB-C   : DFU firmware update (panel-mount USB-C extension on the enclosure side)

 ESP32-C3: WiFi AP + captive web UI (HTTP + WebSocket), BLE-MIDI peripheral, UART-MIDI bridge to Seed3, OTA optional.
```

## 3. Analog front-end / back-end (the only analog design work)

Facts from the Seed3 datasheet (v1.x, pulled 2026-09-21):
- Codec inputs are AC-coupled, **3.6 Vpp typical, ±1.8 V absolute max**. Line input Z is 20 kΩ; outputs 0 dBFS @ 1 Vrms, 100 Ω.
- The datasheet's *Instrument Level* reference circuit (Fig 3.4/3.5, used on the Seed3 Pedal Dev Kit) is: input → 1 MΩ to bias, 10 kΩ pull-down on tip, 100 pF RF cap, OPA1652 unity buffer on a **+9 V single rail** with a mid-rail `AREF_AUDIO_BIAS`, second OPA1652 inverting stage (3k3 / 10k) with 33 nF / 330 pF anti-alias filtering; output stage OPA1652 non-inverting (15 kΩ / 33 kΩ), 10 µF coupling, 100 Ω series, 10 kΩ load.

Design rules to carry into the schematic:
1. **Copy Fig 3.4 / 3.5 verbatim, per channel.** It gives 1 MΩ input impedance, instrument-level headroom and the correct anti-alias filtering; the datasheet publishes its noise/THD plots.
2. **Clamp the codec input.** A +9 V-rail op-amp can swing ~8 Vpp; the codec limit is ±1.8 V. Add a series 100 Ω + Schottky clamps (BAT54S) to AGND/+3V3A at Audio In 1/2, or pick the second-stage gain so full-scale is reached before the op-amp clips. Verify with a hot humbucker/active bass before finalising.
3. **One ground.** AGND and DGND tied at the Seed, single star ground at the DC jack, per datasheet.
4. **Power filtering** exactly as datasheet Fig 1.1 (ferrite + NSR1020 reverse protection + 3R3/100 µF RC on the analog rail). Keep the ESP32 buck on its own filtered branch; radio TX bursts must not reach +9V_A.
5. **Stereo detection**: use the ring-switch on the IN jack; firmware runs dual-mono when a TS plug is present.
6. No relay: bypass is a software pass-through with the effect chain muted, which is what allows delay/reverb trails.

## 4. Footswitches (research summary)

Because bypass is buffered through the DSP, every footswitch carries only a 3.3 V logic level — **electrical life rating is irrelevant; mechanical life and feel are the spec**. Momentary switches also give hold/double-tap gestures for free (effect-select for the knob bank, tap tempo, DFU entry, WiFi-AP start).

| Option | Type | Published life | Notes |
|---|---|---|---|
| **Generic "soft touch" SPST momentary NO** (Guitar Pedal Parts, Love My Switches "Pro-Grade", Daier/Gebildet) | SPST momentary, solder lug | ~50 000 cycles (electrical); users report >100 k | $3–6 each. Quiet, smooth. **Recommended for 5 switches at this budget.** |
| **Lehle SPST momentary soft-click "long life"** (Amplified Parts) | SPST momentary, gold contacts | ≥1 000 000 cycles mech+elec | ~$15–20 each. Upgrade for the 4 effect switches if budget allows. |
| Gorva Design Mechano soft-click | 3PDT latching | 30 000 cycles | Wrong type for this design (latching, MCU can't read state changes cleanly, no gestures). Listed for comparison. |
| Industrial momentary (RS PRO / Mouser foot switches) | SPST, die-cast | often 100k–1M | Too large for a stompbox top; skip. |

Firmware: 5–10 ms software debounce, hold = 500 ms, double-tap window 300 ms. Reserve: hold **tap** = enter effect-select mode; hold **fx1 + fx4** for 2 s = DFU mode (Hothouse convention); hold **tap** 3 s = toggle WiFi AP.

Global HP/LP toggles: 2 × ON-OFF-ON miniature toggles (wiring per the original Seed datasheet Fig 1.9 — two GPIOs with pull-ups, centre = both open; e.g. 2MS3T1B1M2QES). Centre = off; up/down select the two corner frequencies stored from the app.

## 5. DSP methods — analog vs. digital per effect

| Effect | Analog method (for reference) | Chosen digital method | Library / prior art |
|---|---|---|---|
| **Tremolo** | Optical (LDR + lamp, Fender), bias-wiggle, JFET VCA; harmonic tremolo splits bands and modulates anti-phase | LFO × gain, with shape (sine/tri/square/opto-curve) and depth; add **harmonic tremolo** mode (crossover + anti-phase LFOs) — cheap, distinctive | `DaisySP::Tremolo`, `Oscillator` |
| **Delay** | BBD (MN3005) with companding and clock-noise filtering; tape (wow/flutter, head EQ) | Fractional (Hermite/allpass-interpolated) delay line in SDRAM, up to ~2 s stereo; feedback path with the per-effect HP/LP; tape/BBD flavour via wow-flutter LFO + soft-clip + low-pass in feedback; tap tempo with subdivisions; ping-pong when stereo | `DaisySP::DelayLine`, bkshepherd `GuitarPedal` delay modules |
| **Reverb** | Spring tank (transducer + springs), plate | Start with **DaisySP `ReverbSc`** (cheap, good); upgrade path is **CloudSeed-Daisy** (benjaminvdb, runs full-size programs on H750 using MDMA streaming into TCM) or a Dattorro plate. Per-effect HP/LP in the tail, pre-delay, decay, mix. Spring emulation later if wanted (Dattorro-style with dispersive allpass chain). | `DaisySP::ReverbSc`, `benjaminvdb/cloudseed-daisy`, GuitarML/Funbox reverbs |
| **Fuzz** | Fuzz Face (2-Ge/Si transistor feedback pair, input-impedance interaction), Big Muff (4-stage clipping), Tone Bender | Two tiers: (a) **waveshaper fuzz** — input gain → asymmetric tanh/hard-clip with bias & gate → tone (tilt EQ), with **2–4× oversampling + antiderivative anti-aliasing (ADAA)**; (b) **circuit-modelled Fuzz Face** via wave-digital filter (`chowdsp_wdf`, header-only C++, CCRMA WDF Fuzz Face reference) if CPU allows. Neural (LSTM) fuzz is the GuitarML route but training is out of scope for v1. | `chowdsp_wdf`, ADAA (Parker/Zavalishin), GuitarML |
| **HP/LP cuts** | Passive RC / active Sallen-Key | 2nd-order Butterworth biquads (`DaisySP::Svf` or `Biquad`), per-effect (in delay/reverb feedback) and global on output | DaisySP |

Sample rate: run the codec at **48 kHz**, block size 32–48 samples (≈0.7–1 ms per block; total round-trip latency target < 3 ms incl. codec group delay). Oversample only inside the fuzz. 96/192 kHz buys nothing for guitar and quadruples reverb/delay cost.

### CPU budget (480 MHz M7, 48 kHz, stereo) — measure with `daisy::CpuLoadMeter`, targets below are the pass/fail gates

| Block | Target | Fallback if over |
|---|---|---|
| Tremolo (stereo) | < 2 % | — |
| Delay 2 s stereo, interpolated, feedback filters | < 8 % | Linear interpolation; mono delay line + stereo spread |
| Reverb `ReverbSc` | < 15 % | — ; CloudSeed target < 40 % with MDMA streaming, else use a smaller CloudSeed program |
| Fuzz, 4× OS + ADAA, stereo | < 15 % | 2× OS; mono fuzz constrained to before first stereo effect |
| WDF Fuzz Face, stereo | < 30 % | Mono, or waveshaper tier only |
| Control/UART/UI | < 3 % | — |
| **Total headroom** | ≤ 75 % | — |

### Stereo + reorderable chain decision
Process the whole chain in stereo always (2 channels through every effect). This is the simple, correct option and is what makes "fuzz after stereo delay" legal. If the fuzz WDF tier blows the budget, the fallback is a firmware rule that mono-only effects must sit upstream of the first stereo-widening effect (delay/reverb) — the app enforces this in the reorder UI.

## 6. Control architecture

Single protocol everywhere: **MIDI**.
- Knobs, footswitches, toggles → Daisy directly (lowest latency).
- Web UI (WebSocket JSON) and BLE-MIDI both terminate on the ESP32-C3, which converts them to MIDI bytes over UART to the Daisy; Daisy sends state changes back the same way so the web UI and BLE clients stay in sync.
- Parameters = MIDI CC (14-bit CC pairs for time/frequency where 128 steps is too coarse). Chain order, presets, HP/LP corner frequencies = SysEx. Tap tempo = MIDI Clock or a CC.
- **Knob soft-takeover**: after an app/MIDI change, a physical knob does nothing until it passes through the current value. Required because pots are absolute.
- Effect-select for the knob bank: hold TAP then press an effect switch; LED colour/blink shows which effect is bound.
- Presets stored in Seed3 QSPI (libDaisy `PersistentStorage`), ~8 slots; recalled via app, BLE-MIDI Program Change, or double-tap gestures.
- No display was chosen: chain order, preset name and per-effect cut state are visible only in the app; LEDs show effect on/off and tempo.

## 7. Firmware update paths

- **Daisy Seed3**: USB DFU (`dfu-util` or the Electro-Smith web programmer). Panel-mount USB-C passthrough on the enclosure so the box never opens. Enter DFU via the fx1+fx4 hold gesture (libDaisy `System::ResetToBootloader()`) — no need to reach the BOOT button.
- **ESP32-C3**: USB-serial flashing through a second panel USB-C, or **WiFi OTA** (ArduinoOTA / esp_https_ota) once the AP is up. Recommended: OTA as the normal path, USB as recovery.
- Optional later: ESP32 pulls a Daisy firmware image over WiFi and flashes the Daisy through its UART bootloader (STM32 system bootloader supports UART), making the whole pedal updatable wirelessly. Not v1.

## 8. Power budget (9 V barrel, centre negative)

| Load | Rail | Estimate | Source |
|---|---|---|---|
| Seed3 (480 MHz, codec, SDRAM) | VIN 9 V | 100–140 mA | community measurements at 5 V USB; Seed3 regulator dissipation is higher at 9 V — datasheet warns "higher input voltages can increase heat" |
| ESP32-C3, WiFi TX bursts | 3.3 V via buck | 30 mA idle, 250–350 mA peaks | Espressif datasheet class figures |
| 4× OPA1652 + bias | +9 V | ~10 mA | |
| LEDs (5) | 3.3 V | ≤ 25 mA | |
| **Total** | | **~250 mA typical, ~450 mA peak from 9 V** | |

Implications: many isolated pedalboard supply outputs are 100–200 mA; state clearly that this pedal needs a **≥500 mA 9 V output** (e.g. a "high-current" tap). Put a 1 A polyfuse + reverse-protection at the jack. Decide in bench testing whether to feed Seed3 VIN from 9 V directly (simplest, more heat in its LDO) or from the 5 V buck (cooler; buck noise must then be filtered per datasheet Fig 1.4 — 1 mH/3R3 + 100 µF). Recommendation: **feed Seed3 from the 5 V buck**, filtered, and keep the +9 V rail for op-amps only.

## 9. Enclosure and BOM sketch (≈ $250)

| Item | Approx | Notes |
|---|---|---|
| Daisy Seed3 | $30 | |
| ESP32-C3 SuperMini | $5 | |
| Custom carrier PCB (JLCPCB, 2-layer, 5 pcs) | $30 | Seed3 socketed on headers; op-amps SMD |
| 5× soft-touch SPST momentary footswitches | $25 (generic) / $80 (Lehle ×4 + 1 generic) | |
| 6× Alpha 9 mm B10k pots + knobs | $15 | |
| 2× ON-OFF-ON toggles | $6 | |
| 2× TRS 1/4" jacks (Neutrik NMJ6HCD2 or Lumberg) + DC jack | $12 | |
| Op-amps (OPA1652 ×2), buck module, passives, LEDs, USB-C panel mounts | $30 | |
| Enclosure Hammond 1590XX (145×121×39 mm) or 1590DD (188×120×56 mm), powder-coated, drilled | $35–45 | **Open layout check**: 5 footswitches in one row across a 1590XX is ~29 mm pitch — too tight to stomp (40–50 mm is normal). Either 4 effect switches in a row + TAP on a second row (1590XX) or a single row on a 1590DD. 125B is too small either way. |
| **Total** | **≈ $190–245** | |

## 10. Risks and mitigations

1. **Radio EMI into a 1 MΩ input.** Put the ESP32 at the far end from the input jack, antenna facing the enclosure wall, keep the input trace short, shielded input wire, ferrite on the ESP32 supply. Test: noise floor with WiFi AP on vs off.
2. **CPU budget.** Gate each effect on the CPU table in §5 before integration; reverb tier and fuzz tier are the two swap-outs.
3. **WiFi/BLE coexistence on ESP32.** Default BLE-only; WiFi AP on gesture; use ESP-IDF coexistence settings. Fallback: WiFi only when a phone is actively connected.
4. **Codec input overdrive.** Clamp diodes + gain structure (§3 rule 2), decided by the SPICE reference sim in §10b item 6 before the PCB is ordered.
5. **Knob/app fights.** Soft-takeover (§6).
6. **9 V supply headroom.** Document the ≥500 mA requirement; consider a 9–18 V-tolerant buck so a 12 V/18 V tap also works (Seed3 VIN tolerates 17 V; op-amps do not need it).
7. **Seed3 availability.** Currently in stock (179 units on 2026-09-21); an original Seed (rev 7) is a pin-compatible fallback with a PCM3060 codec and the same datasheet limits.

## 10b. Software emulator of the pedal (added per request)

Goal: run the complete pedal — the same DSP code, the same control model (dials, footswitches, toggles, chain order), the same MIDI/SysEx protocol — on the Mac, with audio in/out, so effects can be developed, auditioned and regression-tested before and alongside the hardware.

Architecture — **one DSP core, two hosts**:

```
 firmware/dsp/          pure C++, no libDaisy dependency: effects, chain, parameter model, MIDI/SysEx parser,
                        preset serialisation. Fixed-size float[2] frames, block-based, sample-rate agnostic.
 firmware/daisy/        thin libDaisy wrapper: codec callback → dsp core; GPIO/ADC → parameter model; UART → MIDI parser.
 emulator/              host wrapper: same dsp core compiled natively (arm-none-eabi vs clang), driven by
                        a virtual front panel and real audio I/O.
```

Emulator components:
1. **Audio engine**: CoreAudio/PortAudio (via JUCE or RtAudio) for live guitar through an interface, plus offline mode (`emu --in guitar.wav --out result.wav --preset x.json`) for deterministic regression tests.
2. **Virtual front panel** (decided: **JUCE standalone app + VST3/AU plugin**, same code): 6 dials, 5 footswitches (click = tap, hold = gesture), 2 HP/LP toggles, LEDs, stereo/mono jack state — laid out exactly like the enclosure drawing. The plugin build lets you A/B in a DAW. The emulator additionally exposes the ESP32's WebSocket endpoint so the pedal's web UI can be tested against it (secondary, not the primary panel).
3. **Hardware model layer** (what makes it an *emulator* rather than a plugin): 
   - Codec model: 48 kHz block size = firmware block size, AC coupling, ±1.8 V clip, quantisation to 24/32-bit, measured codec noise floor injected (optional).
   - Analog I/O stage model: 1 MΩ input / OPA1652 stages from §3 as biquads with the datasheet corner frequencies and soft-clip at the rails, so headroom problems are heard on the Mac first. Coefficients derived from an **ngspice** simulation of the §3 schematic (`hardware/spice/`), which also serves as the design check for the analog stage.
   - Pot model: 10k linear/log taper + ADC 12-bit quantisation + smoothing identical to firmware; soft-takeover logic runs here too.
   - CPU model: report per-block cost ratio scaled by a measured Mac↔M7 factor as an early-warning of the §5 budget (not cycle-accurate; the real check is `CpuLoadMeter` on the Seed3).
4. **Control plumbing**: virtual UART — the ESP32 bridge code talks to the emulator over a local TCP socket or a real USB-serial ESP32-C3 on the desk, so BLE-MIDI/WiFi control is testable end-to-end without the Daisy.
5. **Tests**: golden-file audio tests per effect (impulse/sine/guitar clip → compare spectra, aliasing level in fuzz, delay time accuracy vs tap tempo, reverb RT60), run in CI on the host build. Comparisons are within tolerance (spectral/RMS error bounds), not byte-for-byte: the M7 single-precision FPU and clang on Apple Silicon differ in FMA contraction and rounding.

6. **Simple-model vs. full-SPICE validation harness** (`hardware/spice/validate/`). The emulator's analog layer (item 3) is a deliberately minimal "ideal" model: biquads + soft-clip. It is only trustworthy if checked against a full-fidelity simulation of the real circuit, so:
   - **Reference**: ngspice transient simulation of the complete §3 schematic per channel — OPA1652 vendor SPICE macromodel, real bias network, coupling caps, 1 MΩ/10 kΩ input network, a pickup source model (series L ≈ 2–4 H, R ≈ 6–10 kΩ, parallel C ≈ 100 pF, so cable/input loading is included), codec input as 20 kΩ ∥ clamp diodes, output stage into 10 kΩ ∥ 100 pF and into a 1 MΩ amp input.
   - **Stimulus set** (identical WAV files fed to both): log sine sweep 10 Hz–24 kHz at −20 dBFS-equivalent; 1 kHz tones stepped 100 mVpp → 8 Vpp (to find where the op-amp stage vs. the codec clamp limits first); square/impulse for transient/slew behaviour; a DI guitar clip.
   - **Metrics** (Python, `numpy`/`scipy`; ngspice driven via `PySpice` or raw `.cir` + `wrdata`): magnitude/phase response error (target < 0.5 dB / 5° 20 Hz–20 kHz), THD vs. input level, clip onset level and clip shape (waveform correlation), DC offset at the codec pin, output stage load sensitivity.
   - **Loop**: where the simple model misses (typically clip knee shape, sub-20 Hz coupling-cap corner interaction, slew on square edges) fit its parameters to the SPICE result — soft-clip curve fitted to the SPICE transfer curve, biquad corners re-derived — and re-run until within tolerance. The fitted parameters are committed as the emulator's hardware constants, with the SPICE run recorded as the reference.
   - **Also a design tool**: the same reference sim answers the §3 rule-2 question (do we need clamp diodes, or does gain structure alone keep the codec under ±1.8 V?) before the PCB is ordered, and gets re-run against bench measurements of the real board in Phase B (SPICE ↔ hardware ↔ simple model, three-way).
   - Real-time SPICE-in-the-loop remains out of scope; this is an offline comparison.

Memory abstraction: any effect needing large delay memory (delay lines, CloudSeed's MDMA/TCM streaming) takes its buffers through an allocator interface in the DSP core — the Daisy wrapper supplies SDRAM (`DSY_SDRAM_BSS`) and TCM windows, the host supplies heap. Without this the reverb upgrade path and the emulator are mutually exclusive.

Not in scope: cycle-accurate STM32 emulation (QEMU/Renode) — high effort, no benefit for audio; the dsp-core split above gives numerically equivalent behaviour within tolerance where it matters.

License: the whole project is **GPLv3**. This is the free JUCE tier for a one-off personal build and it unlocks the GPL reverbs (Dattorro plate, CloudSeed) that the bkshepherd project has to disable.

## 11. Phased build

**Phase 0 — emulator first (no hardware needed)**
0. Scaffold repo; write `firmware/dsp/` core + `emulator/` host (audio I/O, virtual panel, offline render, golden tests). All four effects and the chain are developed and auditioned here. ngspice model of the analog I/O stage feeds the emulator's hardware layer.

**Phase A — bench prototype (dev boards, breadboard/proto)**
1. Seed3 on a breadboard with the datasheet instrument I/O circuit, one channel; verify loopback audio, noise floor, headroom.
2. Port/write the four effects one at a time in libDaisy/DaisySP; log `CpuLoadMeter` per effect; build the reorderable chain (array of effect pointers, stereo `float[2]` frames, per-effect bypass with trails).
3. ESP32-C3: BLE-MIDI + UART bridge to Daisy; then WiFi AP + captive web UI (single HTML/JS page with WebSocket, stored in SPIFFS/LittleFS); soft-takeover and SysEx preset/order messages.
4. Footswitch gesture layer, tap tempo, effect-select, LEDs.

**Phase B — carrier PCB (KiCad)**
5. Schematic: Seed3 socket, 2× stereo I/O stages, power section (reverse protection, filters, buck), ESP32-C3 socket, pots/switches/LEDs headers, USB-C passthroughs. Reuse Seed3 datasheet figures 1.1, 1.9, 3.4, 3.5 as-is.
6. Layout for 1590XX: input stage far from ESP32; star ground; test points on rails.
7. Order boards, assemble, bring-up with Phase A firmware.

**Phase C — enclosure and polish**
8. Drill template, labels, final firmware, presets, README with DFU/OTA instructions.

Repo layout to create: `docs/` (this research, block diagram, BOM), `hardware/kicad/` + `hardware/spice/` (ngspice analog stage), `firmware/dsp/` (portable DSP core), `firmware/daisy/` (libDaisy wrapper), `firmware/esp32/` (ESP-IDF or PlatformIO Arduino), `emulator/` (host app + tests), `web/` (UI source built into the ESP32 filesystem and served by the emulator).

## 12. Verification

- **Audio path**: sine loopback → measure round-trip latency (scope, expect < 3 ms at 48 kHz/32 samples), noise floor (target ≤ −90 dBFS A-weighted), THD at −6 dBFS, no clipping at codec with hot pickups.
- **CPU**: `CpuLoadMeter` max per block with all four effects on, stereo, worst-case settings (max delay time, max oversampling); must stay ≤ 75 %.
- **Power**: bench supply current at 9 V with WiFi AP active and a client connected; peak ≤ 500 mA; Seed3 and buck temperatures after 30 min.
- **EMI**: noise floor with WiFi on/off, BLE on/off; hum with single-coils.
- **Control**: BLE-MIDI from an iOS/macOS MIDI app changes parameters; web UI reorders chain and it audibly changes; knob soft-takeover behaves; presets survive power-cycle.
- **Switches**: debounce shows zero double-triggers over 200 presses; hold gestures reliable; DFU gesture enters bootloader and `dfu-util -l` sees the device.
- **Mono/stereo**: TS plug in IN → dual-mono; TRS → true stereo; ping-pong delay only in stereo.
- **Emulator fidelity**: simple analog model vs. full ngspice reference (§10b item 6) within 0.5 dB / 5° across 20 Hz–20 kHz and matching clip-onset level within 0.5 dB; golden-file effect tests pass on the host build; in Phase B the same stimulus set is played through the real board and compared three-way (SPICE ↔ hardware ↔ simple model).
- **Power section**: Seed3 fed from the filtered 5 V buck output boots reliably and the buck's switching noise is absent from the audio noise floor (compare against Seed3 on bench 5 V).

## Sources

- Seed3 product/docs: https://docs.daisy.audio/hardware/Seed3/ · https://daisy.audio/products/seed3 · datasheet https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed3/Daisy_Seed3_datasheet.pdf
- Original Seed datasheet (limits, codec app note): https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed/Daisy_Seed_datasheet.pdf
- Seed current draw thread: https://community.daisy.audio/t/power-consumption/4521
- 9 V to VIN thread: https://forum.electro-smith.com/t/connecting-9v-directly-to-daisy-seed/1279
- Instrument input thread: https://community.daisy.audio/t/instrument-input-for-daisy/9095
- Pedal hardware summary: https://community.daisy.audio/t/summary-of-daisy-seed-guitar-pedal-hardware-options/7336
- Hothouse: https://clevelandmusicco.com/hothouse-diy-digital-signal-processing-platform-kit/ · https://github.com/clevelandmusicco/HothouseExamples
- bkshepherd GuitarPedal (chain, presets, MIDI, tap tempo): https://github.com/bkshepherd/DaisySeedProjects/blob/main/Software/GuitarPedal/README.md
- Daisy + ESP32 pedal: https://github.com/TMANTsmith/daisy-esp32-guitar-pedal · https://community.daisy.audio/t/esp32-control-of-daisy-seed/1226
- CloudSeed on Daisy: https://github.com/benjaminvdb/cloudseed-daisy
- WDF Fuzz Face / chowdsp_wdf: https://github.com/Chowdhury-DSP/chowdsp_wdf · https://ccrma.stanford.edu/wiki/Wave_Digital_Filters_applied_to_the_Dunlop_%22Fuzz_Face%22_Distortion_Circuit · https://arxiv.org/pdf/2009.02833
- Antialiasing for distortion: https://www.kvraudio.com/forum/viewtopic.php?t=445438 · https://arxiv.org/pdf/2505.11375
- ESP32 BLE-MIDI: https://github.com/max22-/ESP32-BLE-MIDI · coexistence: https://esp32.com/viewtopic.php?t=16747
- ESP32 C3 vs S3 SuperMini: https://www.espboards.dev/blog/esp32-super-mini-comparison/
- Footswitches: https://www.amplifiedparts.com/products/footswitch-lehle-spst-momentary-soft-click-long-life · https://lovemyswitches.com/pro-grade-spst-momentary-foot-switch-normally-closed-soft-touch-solder-lug/ · https://guitarpedalparts.com/products/spst-momentary-soft-touch-foot-switch · https://www.amplifiedparts.com/products/footswitch-3pdt-g-rva-design-mechano-soft-click · https://forum.pedalpcb.com/threads/spst-footswitch-guidance.7701/

---

## Phase 0 results (2026-09-21)

Implemented: `firmware/dsp` core (all four effects, chain, cuts, tap tempo, presets, MIDI map),
`emulator/` (offline CLI + hardware model + 45 property tests + JUCE panel), `hardware/spice/`
(ngspice reference + validation harness). Findings that change the design:

1. **Input-stage gain is −6.2 dB, not −4.6 dB.** The second stage's 10 kΩ input resistor loads the
   3k3/4k7 divider (4k7 ‖ 10k). Net pedal gain jack→jack with the datasheet stages is ≈ 0.49 × (1.414/1.8) × 2.2 ≈ 0.85 (−1.4 dB); set `outputTrimDb` ≈ +1.4 dB in firmware for unity, or change R2 in the output stage to 39 k.
2. **Codec full scale / abs max reached before op-amp clipping.** The Seed3 codec's ±1.8 V is both its 0 dBFS swing (Table 3) and its absolute-maximum rating (Table 1). The codec pin reaches it at ≈3.7 V pk at the jack; the op-amps stay clean to ≈4.4 V pk. So the pedal's input ceiling is ≈3.7 V pk (fine for passive guitar, marginal for hot active bass) and, past it, the op-amps can drive the codec beyond its rating. Decision: **fit BAT54S Schottky clamps** (to AGND / +3V3A) at each codec input on the carrier board — they protect the codec, they do not add headroom (§3 rule 2 resolved). More headroom would need a lower second-stage gain plus a matching output-trim change.
3. **Fuzz CPU.** 4× oversampled ADAA fuzz with 128-tap FIRs costs 1.6 % of real time on an M4 Pro; the other three effects together < 0.3 %. Extrapolated to the M7 this is the block most likely to exceed the §5 budget — measure first on hardware, fall back to 2× (`Oversample` param) if `CpuLoadMeter` max > 75 %.
4. **Emulator fidelity vs SPICE** (behavioral OPA1652 substitute): input stage 0.39 dB / 4.9° (phase passes marginally, in the 20 kHz band edge where the sweep is fading), output 0.32 dB / 1.6°, identical clip onset, THD tracking within 0.1 % absolute. Passes §12. Re-run against the TI macromodel when downloaded, and against the real board in Phase B.
5. Anti-aliasing measured: hard-clip fuzz non-harmonic energy −28 dB (1×, ADAA only) → −42 dB (4× + ADAA).
