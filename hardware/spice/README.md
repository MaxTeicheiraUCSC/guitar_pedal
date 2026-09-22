# hardware/spice — ngspice reference for the analog I/O stages

Netlists transcribed from the Seed3 datasheet instrument-level circuits
(Fig 3.4 input, Fig 3.5 output, OPA1652 on +9 V, AREF bias 4.5 V):

- `input_stage.cir` — jack tip → codec pin. Stimulus and output paths are
  substituted by `validate/run_spice.py` (`{STIM}`, `{OUT}`, `{TSTEP}`, `{TSTOP}`, `{MODELS}`).
- `output_stage.cir` — DAC → output jack into a 1 MΩ ∥ 500 pF amp input.
- `pickup_loading.cir` — standalone AC check of pickup/cable resonance into the 1 MΩ input.
- `models/` — op-amp models (see `models/README.md`: the TI model is not bundled).

Topology notes (from reading the datasheet drawing, not the text dump):
- The 10 kΩ at the jack is on the **tip-normal** contact — it grounds an unplugged
  input and is out of circuit with a plug inserted, so Zin is the 1 MΩ bias resistor.
- The 3k3/4k7 divider sits between the buffer and the second stage: gain 0.5875,
  RC corner (3k3‖4k7)·1 nF ≈ 82 kHz. The 4k7 goes to ground, so the DC at that node
  is 0.5875 × 4.5 V; the 10 µF re-couples to the second stage's 4.5 V reference.
- Second stage is inverting, 10k/10k ‖ 330 pF (48 kHz); output RC 100 Ω/33 nF (48 kHz).
- Codec input: AC coupled, 20 kΩ typical. **No clamp is drawn**; the codec's
  ±1.8 V absolute maximum is checked by measurement in the validation report.
- Output: inverting 33k/15k = −2.2 ‖ 100 pF (48 kHz), 100 Ω series, 10 µF, 10 kΩ.

Unknowns carried as assumptions (flagged in the report): AREF bias exactly 4.5 V and
stiff; codec coupling capacitor value (4.7 µF assumed); codec input capacitance ignored.

Validation harness: see `validate/README.md`.
