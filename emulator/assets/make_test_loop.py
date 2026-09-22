#!/usr/bin/env python3
"""Render test_loop.wav — a 16-bar clean-guitar loop built from the FreePats
"FSBS Electric Guitar Clean" note samples (CC0 1.0, https://freepats.zenvoid.org/ElectricGuitar/).
Real Fender DI recordings, pitch-shifted at most a few semitones. Output: mono 44.1 kHz 16-bit,
peak ≈ 0.5 (≈ a hot passive pickup when the emulator's input level is 1 Vpk).

Usage: python3 make_test_loop.py <dir with the FLAC samples>   (needs ffmpeg, numpy, scipy)
"""
import sys, os, subprocess, io
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

SR = 44100
NOTE_NAMES = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}

def midi_of(name):            # "C#6" -> 85
    p = name[:-1]; o = int(name[-1]); return 12 * (o + 1) + NOTE_NAMES[p]

def load_flac(path):
    raw = subprocess.run(["ffmpeg", "-v", "quiet", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "wav", "-"], capture_output=True, check=True).stdout
    sr, d = wavfile.read(io.BytesIO(raw)); d = d.astype(np.float64)
    if d.dtype != np.float64: pass
    return d / (32768.0 if d.max() > 1.5 else 1.0)

def make_e_gsharp(samples, keys):
    """E3 for 5 s, then G#3 for 5 s (each: pluck, re-pluck at 2.5 s so the note is still audible), 10 s seamless loop."""
    def note(midi, dur, vel=1.0):
        k = keys[np.argmin(np.abs(keys - midi))]; s = samples[k]; semis = midi - k
        if semis:
            r = 2 ** (semis / 12); n = max(8, int(round(1000 / r))); s = resample_poly(s, 1000, n)
        out = s[: int(dur * SR)].copy(); fade = min(len(out), int(0.02 * SR)); out[-fade:] *= np.linspace(1, 0, fade); return out * vel
    total = np.zeros(int(10 * SR) + SR)
    def put(t, sig): i = int(t * SR); n = min(len(sig), len(total) - i); total[i:i + n] += sig[:n]
    for t0, midi in ((0.0, 52), (5.0, 56)):          # E3 = 52, G#3 = 56
        put(t0, note(midi, 2.6, 1.0)); put(t0 + 2.5, note(midi, 2.6, 0.85))
        put(t0, note(midi - 12, 2.6, 0.5)); put(t0 + 2.5, note(midi - 12, 2.6, 0.4))   # octave below for body
    total = total[: int(10 * SR)]
    xf = int(0.05 * SR); w = np.linspace(0, 1, xf); total[:xf] = total[:xf] * w + total[-xf:] * (1 - w); total = total[:-xf]
    total *= 0.5 / np.abs(total).max()
    wavfile.write(os.path.join(os.path.dirname(__file__), "test_e_gsharp.wav"), SR, (total * 32767).astype(np.int16))
    print(f"wrote test_e_gsharp.wav: {len(total) / SR:.1f} s (E3 0-5 s, G#3 5-10 s)")

def make_emaj7_arp(samples, keys):
    """E major 7 arpeggio, plucked eighth notes at 92 bpm: E3 G#3 B3 D#4 E4 D#4 B3 G#3, 4 bars, seamless 10.4 s loop."""
    def note(midi, dur, vel=1.0):
        k = keys[np.argmin(np.abs(keys - midi))]; s = samples[k]; semis = midi - k
        if semis:
            r = 2 ** (semis / 12); n = max(8, int(round(1000 / r))); s = resample_poly(s, 1000, n)
        out = s[: int(dur * SR)].copy(); fade = min(len(out), int(0.02 * SR)); out[-fade:] *= np.linspace(1, 0, fade); return out * vel
    bpm = 92; beat = 60 / bpm; step = beat / 2
    pattern = [52, 56, 59, 63, 64, 63, 59, 56]               # E3 G#3 B3 D#4 E4 D#4 B3 G#3
    bars = 4; total = np.zeros(int(bars * 8 * step * SR) + SR)
    def put(t, sig): i = int(t * SR); n = min(len(sig), len(total) - i); total[i:i + n] += sig[:n]
    for bar in range(bars):
        for k, m in enumerate(pattern):
            t = (bar * 8 + k) * step
            put(t, note(m, 1.6, 1.0 if k % 2 == 0 else 0.8))
        put(bar * 8 * step, note(40, 3.0, 0.55))                # low E2 under each bar
    total = total[: int(bars * 8 * step * SR)]
    xf = int(0.05 * SR); w = np.linspace(0, 1, xf); total[:xf] = total[:xf] * w + total[-xf:] * (1 - w); total = total[:-xf]
    total *= 0.5 / np.abs(total).max()
    wavfile.write(os.path.join(os.path.dirname(__file__), "test_emaj7_arp.wav"), SR, (total * 32767).astype(np.int16))
    print(f"wrote test_emaj7_arp.wav: {len(total) / SR:.1f} s Emaj7 arpeggio at {bpm} bpm")

def write_note_samples(samples):
    """One 2 s WAV per recorded note for the emulator's on-screen keyboard sampler (note_<midi>.wav)."""
    d = os.path.join(os.path.dirname(__file__), "samples"); os.makedirs(d, exist_ok=True)
    for midi, s in samples.items():
        out = s[: int(2.0 * SR)].copy(); fade = min(len(out), int(0.05 * SR)); out[-fade:] *= np.linspace(1, 0, fade)
        out *= 0.7 / max(1e-9, np.abs(out).max())
        wavfile.write(os.path.join(d, f"note_{midi}.wav"), SR, (out * 32767).astype(np.int16))
    print("wrote", len(samples), "note samples:", sorted(samples))

def main(srcdir):
    samples = {}
    for f in sorted(os.listdir(srcdir)):
        if f.endswith(".flac"): samples[midi_of(f.split("_")[0])] = load_flac(os.path.join(srcdir, f))
    keys = np.array(sorted(samples))
    def note(midi, dur, vel=1.0):
        k = keys[np.argmin(np.abs(keys - midi))]; s = samples[k]; semis = midi - k
        if semis:   # pitch shift by resampling (changes length too, fine for a plucked note)
            r = 2 ** (semis / 12); n = max(8, int(round(1000 / r))); s = resample_poly(s, 1000, n)
        out = s[: int(dur * SR)].copy()
        fade = min(len(out), int(0.02 * SR)); out[-fade:] *= np.linspace(1, 0, fade)
        return out * vel
    bpm = 96; beat = 60 / bpm; bar = 4 * beat
    total = np.zeros(int(16 * bar * SR) + SR)
    def put(t, sig): i = int(t * SR); n = min(len(sig), len(total) - i); total[i:i + n] += sig[:n]
    # Am  F  C  G  |  arpeggios (bars 1-8), then strummed chords + melody (bars 9-16)
    chords = [[45, 52, 57, 60], [41, 48, 53, 57], [48, 52, 55, 60], [43, 50, 55, 59]]
    for bar_i in range(8):
        ch = chords[bar_i % 4]; t0 = bar_i * bar
        pat = [0, 1, 2, 3, 2, 1, 2, 3]
        for k, idx in enumerate(pat): put(t0 + k * beat / 2, note(ch[idx], 1.2, 0.8 if k % 2 else 1.0))
    melody = [[57, 60, 64, 62], [60, 57, 55, 57], [60, 64, 67, 64], [62, 59, 55, 59]]
    for bar_i in range(8, 16):
        ch = chords[bar_i % 4]; t0 = bar_i * bar
        for hit in (0, 1.5, 2, 3):                              # strum with ~12 ms string spread
            for j, n in enumerate(ch): put(t0 + hit * beat + j * 0.012, note(n, 1.4, 0.55 if hit else 0.7))
        for k, m in enumerate(melody[bar_i % 4]): put(t0 + k * beat + beat * 0.5, note(m + 12, 0.9, 0.9))
    total = total[: int(16 * bar * SR)]
    # seamless loop: crossfade the last 50 ms into the start
    xf = int(0.05 * SR); w = np.linspace(0, 1, xf); total[:xf] = total[:xf] * w + total[-xf:] * (1 - w); total = total[:-xf]
    total *= 0.5 / np.abs(total).max()
    wavfile.write(os.path.join(os.path.dirname(__file__), "test_loop.wav"), SR, (total * 32767).astype(np.int16))
    print(f"wrote test_loop.wav: {len(total) / SR:.1f} s at {bpm} bpm")
    make_e_gsharp(samples, keys)
    make_emaj7_arp(samples, keys)
    write_note_samples(samples)

if __name__ == "__main__":
    main(sys.argv[1])
