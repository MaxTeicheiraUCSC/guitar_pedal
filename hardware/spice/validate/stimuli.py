"""Stimulus set shared by both models (48 kHz float WAV, values in volts)."""
import numpy as np, os
from scipy.io import wavfile
from scipy.signal import resample_poly
import config as C

def log_sweep(f0=20.0, f1=20000.0, dur=0.25, amp=0.1, fs=C.FS):
    t = np.arange(int(dur * fs)) / fs
    k = np.log(f1 / f0) / dur
    x = amp * np.sin(2 * np.pi * f0 * (np.exp(k * t) - 1) / k)
    fade = int(0.002 * fs); w = np.ones_like(x); w[:fade] = np.linspace(0, 1, fade); w[-fade:] = np.linspace(1, 0, fade)
    return (x * w).astype(np.float32)

def level_steps(levels, f=1000.0, seg=0.025, fs=C.FS):
    out = []
    for a in levels:
        t = np.arange(int(seg * fs)) / fs
        s = a * np.sin(2 * np.pi * f * t)
        fade = int(0.003 * fs); w = np.ones_like(s); w[:fade] = np.linspace(0, 1, fade); w[-fade:] = np.linspace(1, 0, fade)
        out.append(s * w)
    return np.concatenate(out).astype(np.float32)

def bl_square(f=200.0, amp=0.5, dur=0.05, fs=C.FS):
    t = np.arange(int(dur * fs)) / fs
    x = np.zeros_like(t)
    k = 1
    while k * f < 20000:
        x += np.sin(2 * np.pi * k * f * t) / k; k += 2
    x *= 4 / np.pi * amp
    return x.astype(np.float32)

def pluck(dur=0.5, amp=0.6, fs=C.FS, seed=3):
    rng = np.random.default_rng(seed); y = np.zeros(int(dur * fs), dtype=np.float64)
    for f0, t0 in [(110.0, 0.0), (164.8, 0.2)]:
        N = int(fs / f0); buf = rng.uniform(-1, 1, N); out = np.zeros(int(0.5 * fs))
        for n in range(len(out)):
            out[n] = buf[n % N]; buf[n % N] = 0.995 * 0.5 * (buf[n % N] + buf[(n + 1) % N])
        s = int(t0 * fs); L = min(len(out), len(y) - s); y[s:s + L] += out[:L]
    y = np.tanh(1.2 * y); y *= amp / np.abs(y).max()
    return y.astype(np.float32)

STIMULI = {
    "in": {
        "sweep":  lambda: log_sweep(f0=10.0, f1=22000.0, amp=0.1),
        "steps":  lambda: level_steps([0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 3.0, 3.5, 4.0, 4.5, 5.0, 6.0, 8.0]),
        "square": lambda: bl_square(amp=0.5),
        "pluck":  lambda: pluck(amp=0.6),
    },
    "out": {   # DAC domain: 1.0 = 0 dBFS
        "sweep":  lambda: log_sweep(f0=10.0, f1=22000.0, amp=0.1),
        "steps":  lambda: level_steps([0.05, 0.1, 0.2, 0.35, 0.5, 0.7, 0.85, 0.95, 1.0]),   # DAC cannot exceed 0 dBFS
        "square": lambda: bl_square(amp=0.5),
        "pluck":  lambda: pluck(amp=0.7),
    },
}

def write_all():
    os.makedirs(C.OUT_DIR, exist_ok=True)
    paths = {}
    for stage, table in STIMULI.items():
        for name, fn in table.items():
            x = fn()
            wav = os.path.join(C.OUT_DIR, f"{stage}_{name}.wav")
            wavfile.write(wav, C.FS, x)
            # PWL text for ngspice: band-limited upsample to FS_PWL, plus 2 ms of pre-roll
            scale = C.DAC_FULL_SCALE_V if stage == "out" else 1.0   # DAC domain -> volts for SPICE
            up = resample_poly(x.astype(np.float64) * scale, C.FS_PWL // C.FS, 1)
            pre = np.zeros(int(0.002 * C.FS_PWL))
            sig = np.concatenate([pre, up, np.zeros(int(0.001 * C.FS_PWL))])
            t = np.arange(len(sig)) / C.FS_PWL
            txt = os.path.join(C.OUT_DIR, f"{stage}_{name}.pwl")
            np.savetxt(txt, np.column_stack([t, sig]), fmt="%.9g")
            paths[(stage, name)] = (wav, txt, len(x) / C.FS, len(pre) / C.FS_PWL)
    return paths

if __name__ == "__main__":
    for k, v in write_all().items(): print(k, v)
