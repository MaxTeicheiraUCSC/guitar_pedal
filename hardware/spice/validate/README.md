# validate — simple analog model vs full ngspice reference

`python3 run_all.py` (needs `ngspice`, numpy/scipy/matplotlib, and `emulator/build/emu`):

1. `stimuli.py` writes the shared stimulus set as 48 kHz float WAV (volts) and as
   384 kHz PWL text for ngspice: 10 Hz–22 kHz log sweep at 0.1 V, 1 kHz level steps
   (50 mV → 8 V at the jack; 0.05 → 1.0 FS for the DAC), a band-limited 200 Hz square,
   and a synthetic plucked-string clip.
2. `run_models.py` runs `input_stage.cir` / `output_stage.cir` in ngspice (batch,
   `linearize`d to 192 kHz) and `emu --stage in|out` (the simple model, codec clamp
   disabled for the comparison).
3. `compare.py` decimates the SPICE output to 48 kHz, aligns the two, and reports:
   1/12-octave magnitude/phase error, THD and gain per level step, 1 dB compression
   onset, the input level at which the codec pin exceeds ±1.8 V, and normalised RMS
   waveform error. Output: `out/report.md`, `out/metrics.json`, PNG plots.

Pass criteria (docs/design.md §12): |mag| < 0.5 dB, |phase| < 5° over 20 Hz–20 kHz,
clip onset within 0.5 dB.

What the harness found so far (2026-09-21, behavioral op-amp model):
- The hand-derived input attenuation (0.5875) was wrong: the second stage's 10 kΩ input
  resistor loads the 4k7 leg → 0.492 (−6.2 dB). `AnalogParams::inAtten` was corrected.
- Both stages then pass: input 0.39 dB / 4.9°, output 0.32 dB / 1.6°, identical clip
  onset (5 V pk), THD vs level within 0.1 % absolute, NRMSE ≈ 2 % on program material.
- Design check: with the datasheet circuit the codec pin reaches ±1.8 V at ≈3.7 V pk
  at the jack while the op-amps stay clean to ≈4.4 V pk → add Schottky clamps at the
  codec input (or lower the second-stage gain) for active-pickup safety.

To use TI's OPA1652 model instead of the behavioral substitute, see `../models/README.md`.
Bench measurements of the real board go through the same `compare.py` path: record the
stimulus WAVs through the pedal and drop them in as a third "model".
