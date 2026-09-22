# guitar_pedal — DSP multi-effect (tremolo · delay · reverb · fuzz)

Daisy Seed3 + ESP32-C3 stereo pedal with a reorderable four-effect chain, five
momentary footswitches, a shared six-knob bank, global high/low-cut toggles, and
BLE-MIDI / WiFi web-UI control. Design rationale, research and the phased plan are
in [`docs/design.md`](docs/design.md); the control protocol in [`docs/midi-map.md`](docs/midi-map.md).

```
firmware/dsp/      portable DSP core (header-only C++17) — shared by the pedal and the emulator
firmware/daisy/    Seed3 wrapper: audio callback, switches, knobs, UART-MIDI, presets   [unverified: no ARM toolchain here]
firmware/esp32/    C3 bridge: BLE-MIDI + WiFi AP web UI ⇄ UART-MIDI                   [unverified: no ESP toolchain here]
web/               single-file web UI served by the ESP32
emulator/          macOS emulator: `emu` offline CLI, hardware model, JUCE front panel (Standalone + VST3), tests
hardware/spice/    ngspice reference of the Seed3 instrument I/O stages + validation harness
hardware/kicad/    carrier board (not started)
docs/              design.md, midi-map.md
```

## Build & test (macOS)

```sh
cd emulator && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
./build/dsp_tests                                   # 45 property tests
./build/emu --list                                  # parameter tables + CC numbers
./build/emu --in guitar.wav --out fx.wav --enable 0,1,2,3 --order 3,0,1,2 --hw --cpu
cmake -B build-juce -DPEDAL_BUILD_JUCE=ON && cmake --build build-juce -j8   # JUCE app + VST3 (fetches JUCE 8.0.9)
cmake -B build-juce -DPEDAL_BUILD_JUCE=ON -DPEDAL_HARNESS=ON && ./build-juce/juce/PedalHarness_artefacts/Release/PedalHarness   # in-process UI/audio stress test
cd ../hardware/spice/validate && python3 run_all.py # simple model vs ngspice → out/report.md
```

Handover: **[`docs/STATUS.md`](docs/STATUS.md)** (what is and is not verified, next steps) and
**[`docs/LEARNINGS.md`](docs/LEARNINGS.md)** (the non-obvious things this cost time to find).

## Status (2026-09-22)

- DSP core, emulator CLI, property tests, SPICE reference and validation harness: **done and passing**.
- Validation found and fixed a real error in the hand-derived analog model (input gain −6.2 dB, not −4.6 dB)
  and shows the codec's ±1.8 V limit is reached at ≈3.7 V pk at the jack, before the op-amps clip
  → the carrier board gets Schottky clamps at the codec input.
- JUCE front panel: builds Standalone + VST3 (AU needs full Xcode). Default source is an embedded CC0 clean-guitar loop (`emulator/assets`), so no interface is needed to audition; live input is a menu choice. `PedalHarness` drives the real processor + editor with concurrent audio at 44.1/48/96 kHz and passes.
- Daisy / ESP32 firmware: written, **not compiled** (toolchains not installed on this machine).
- KiCad carrier board: generated from one netlist, routed, 0 DRC errors with schematic parity; gerbers/STEP/BOM in `hardware/kicad/out/`.
- Windows VST3 v0.1.3 links the CRT statically and ships an installer — earlier builds needed the VC++ redistributable and failed to load without it.
- Hardware: nothing fabricated. First hardware task is the Phase A bench measurement in `docs/design.md` §11.
- A project guard (`.claude/settings.json` + `.claude/hooks/guard.py`) confines Claude Code sessions in this repo to this repo; see `docs/STATUS.md`.

## Windows / macOS binaries

`.github/workflows/build.yml` builds the emulator on `windows-latest` (MSVC) and `macos-latest`, runs the
DSP tests, and uploads `PedalEmu-windows-x64.zip` (installer + PedalEmu.vst3 + standalone) and the setup .exe and `PedalEmu-macos.zip`
as workflow artifacts; pushing a tag `v*` publishes them as a GitHub Release. Windows install/troubleshooting: `docs/WINDOWS-INSTALL.md`. The Windows job also builds an
Inno Setup installer, checks the binaries have no dynamic MSVC-runtime dependency, and re-validates
the plugin after extracting the shipped zip. Local Windows build:
`cd emulator && cmake -B build -DPEDAL_BUILD_JUCE=ON && cmake --build build --config Release --target PedalEmu_Standalone PedalEmu_VST3`
(Visual Studio 2022 Build Tools; JUCE 8.0.9 is fetched by CMake). The Windows build has only been
exercised in CI — see the Actions tab for the current status.

License: GPLv3 (see `LICENSE`) — this is the free JUCE tier and permits the GPL reverb algorithms.
