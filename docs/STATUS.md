# Project status — 2026-09-22

Snapshot for picking this up in a new session. Honest about what is proven and what is not.

## Where things stand

| Area | State |
|---|---|
| DSP core (`firmware/dsp`) | **Done, tested.** Tremolo (incl. harmonic), 2 s stereo delay, Dattorro plate reverb, oversampled ADAA fuzz, reorderable chain with trails, global HP/LP, tap tempo, presets, MIDI map. 46 property tests pass. |
| Emulator CLI (`emulator/cli`) | **Done.** `emu` renders WAV through the pedal, optional analog+codec model, per-effect CPU cost. |
| Emulator GUI (JUCE) | **Builds and runs** on macOS and Windows: Standalone + VST3. Loop player (3 built-in CC0 loops + load a file), on-screen keyboard sampler, BPM entry, soft-takeover knobs. |
| SPICE validation (`hardware/spice`) | **Done, passing.** ngspice reference vs the emulator's analog model: 0.39 dB / 4.9° (input), 0.32 dB / 1.6° (output), matching clip onset. |
| KiCad board (`hardware/kicad`) | **Generated and routed.** 150 × 102 mm 2-layer, 111 footprints, 77 nets, all routed, **0 DRC errors** with schematic parity. Exports in `hardware/kicad/out/` (STEP, gerbers+drill, BOM, PDF, renders). |
| Daisy firmware (`firmware/daisy`) | **Written, never compiled** — no `arm-none-eabi-gcc` on this Mac. Expect libDaisy API-name fixes. |
| ESP32 firmware (`firmware/esp32`) | **Written, never compiled** — no PlatformIO/ESP-IDF here. |
| Web UI (`web/index.html`) | Written, never run against a real ESP32 bridge. |
| CI + releases | **Working.** GitHub Actions builds macOS + Windows on every push; a `v*` tag publishes a release. Latest: v0.1.3. |

## Verified vs unverified

Verified by something other than my say-so:
- 46 DSP property tests (delay lands the impulse at exactly N samples, RT60 grows with decay, 14-bit CC, preset round-trip, aliasing floor, …).
- `PedalHarness` drives the real processor + editor with concurrent audio at 44.1/48/96 kHz.
- SPICE-vs-model comparison report (`hardware/spice/validate/out/report.md`).
- KiCad ERC 0 violations; netlist parity gate (`tools/check_netlist.py`); `kicad-cli pcb drc --schematic-parity` 0 errors.
- Windows VST3: pluginval strictness 8 SUCCESS, standalone launched and screenshotted on the runner, published zip re-extracted and re-validated, no dynamic MSVC-runtime dependency.

**Not** verified:
- The plugin has never been loaded in a DAW by hand (Roman is testing; see below).
- No firmware has been compiled or run on hardware.
- No PCB has been fabricated; enclosure fit, ESP32-C3 SuperMini row spacing (placed at 15.24 mm) and the jack/pot mechanical fit are all unconfirmed.
- The emulator's analog model is validated against *SPICE*, not against a real board.

## Immediate next steps

1. **Roman's Windows test.** v0.1.3 fixed a real defect (see `docs/LEARNINGS.md`, "Windows packaging"). Ask him: which DAW and is it 64-bit; does the installer route work. No email has been sent about v0.1.3.
2. **Hardware Phase A** (`docs/design.md` §11): breadboard the Seed3 with the datasheet instrument I/O, measure noise floor, latency, current draw, and `CpuLoadMeter` per effect. The fuzz at 4× oversampling is the block most likely to blow the CPU budget — fall back to 2× if max load > 75 %.
3. **Toolchains** to install before firmware can be built: `arm-none-eabi-gcc` + libDaisy, PlatformIO (ESP32), `dfu-util`.
4. **Board review** before ordering: ESP32 row spacing, 5-footswitch pitch vs enclosure (1590XX at ~29 mm is too tight — 4+1 rows or 1590DD), and the Seed3 3D model is not attached to U1.

## Resuming work

```sh
cd emulator && cmake -B build -DPEDAL_BUILD_JUCE=ON && cmake --build build -j8   # CLI + tests + GUI
./build/dsp_tests                                                                 # 46 property tests
cmake -B build -DPEDAL_HARNESS=ON && ./build/juce/PedalHarness_artefacts/Release/PedalHarness
cd ../hardware/spice/validate && python3 run_all.py                               # SPICE comparison
cd ../../kicad && python3 tools/gen_sch.py && ./route.sh                          # regenerate + route the board
/usr/bin/python3 .claude/hooks/test_guard.py                                      # 64 guard cases
```

## Session-specific things that did **not** survive

- A 10-minute cron loop that watched Gmail for bug reports from Roman (deleted on request; it only ever found no mail, and sent nothing).
- Nothing else — everything of value is committed.

## The repo guard is active and persists

`.claude/settings.json` + `.claude/hooks/guard.py` confine any Claude Code session in this repo:
file tools to the repo and scratchpad, Bash to a build/test/git/gh allowlist, Gmail to in-thread
replies with a secret scan, and other connectors/web off. It is enforced by the harness, not by
good intentions. **It will block work outside this repo** — including writing to Claude's own
memory directory. Relax or remove `.claude/settings.json` if that gets in the way.
