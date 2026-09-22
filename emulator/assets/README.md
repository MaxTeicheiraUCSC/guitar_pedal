# assets

- `test_loop.wav` — 40 s clean electric-guitar loop (Am–F–C–G arpeggios then strums + melody,
  96 bpm, seamless), mono 44.1 kHz 16-bit, peak 0.5. Built by `make_test_loop.py` from the
  FreePats **FSBS Electric Guitar Clean** note samples (real Fender DI recordings), released under
  **CC0 1.0** — see `FREEPATS-LICENSE-CC0.txt` and https://freepats.zenvoid.org/ElectricGuitar/.
  The loop itself is therefore also CC0. It is embedded in the PedalEmu app as the default source.
- `test_e_gsharp.wav` — 10 s loop: E3 (with E2 under it) for 5 s, then G♯3 for 5 s, each plucked at 0 and 2.5 s. Same CC0 samples. **Default source in PedalEmu.**
- `test_emaj7_arp.wav` — 10.4 s loop: E major 7 arpeggio (E3 G♯3 B3 D♯4 E4 D♯4 B3 G♯3) in plucked eighth notes at 92 bpm with a low E2 under each bar. Same CC0 samples.
- Regenerate: download the `.flac` files from https://github.com/freepats/electric-guitar-FSBS-clean/tree/main/samples/bridge/small
  into a folder and run `python3 make_test_loop.py <folder>` (needs ffmpeg).
