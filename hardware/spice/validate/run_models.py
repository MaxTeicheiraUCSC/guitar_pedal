"""Run ngspice (reference) and the emulator's simple model on every stimulus."""
import os, subprocess, sys, time
import numpy as np
from scipy.io import wavfile
import config as C

def run_spice(stage, stim_pwl, out_txt, tstop):
    netlist, vec, _ = C.STAGES[stage]
    src = open(os.path.join(C.SPICE_DIR, netlist)).read()
    models = C.MODELS_DIR
    if C.OPAMP_MODEL == "ti":
        src = src.replace("opa1652_behavioral.lib", "opa1652_ti.lib").replace("OPA1652_BEHAV", "OPA1652")
    deck = (src.replace("{MODELS}", models).replace("{STIM}", stim_pwl).replace("{OUT}", out_txt)
               .replace("{TSTEP}", repr(1.0 / C.FS_SPICE)).replace("{TSTOP}", repr(tstop)))
    deck_path = out_txt.replace(".txt", ".cir")
    open(deck_path, "w").write(deck)
    t0 = time.time()
    r = subprocess.run([C.NGSPICE, "-b", deck_path], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out_txt):
        sys.stderr.write(r.stdout[-2000:] + r.stderr[-2000:]); raise RuntimeError(f"ngspice failed for {deck_path}")
    data = np.loadtxt(out_txt, skiprows=1)   # wr_vecnames: header line; wr_singlescale: one time column
    header = open(out_txt).readline().split()
    col = header.index(vec)
    return data[:, 0], data[:, col], time.time() - t0

def run_simple(stage, stim_wav, out_wav, overrides):
    args = [C.EMU, "--in", stim_wav, "--out", out_wav, "--stage", C.STAGES[stage][2]]
    for k, v in overrides.items(): args += ["--hwparam", f"{k}={v}"]
    subprocess.run(args, check=True)
    fs, y = wavfile.read(out_wav)
    return y.astype(np.float64)

def run_all(paths):
    results = {}
    for (stage, name), (wav, pwl, dur, preroll) in paths.items():
        out_txt = os.path.join(C.OUT_DIR, f"{stage}_{name}_spice.txt")
        t, v, secs = run_spice(stage, pwl, out_txt, dur + preroll + 0.001)
        simple = run_simple(stage, wav, os.path.join(C.OUT_DIR, f"{stage}_{name}_simple.wav"), C.COMPARE_OVERRIDES[stage])
        # drop pre-roll, remove DC (both are AC-coupled: compare the AC part)
        v = v[t >= preroll - 1e-9][: int(dur * C.FS_SPICE)]
        results[(stage, name)] = dict(t_spice=np.arange(len(v)) / C.FS_SPICE, spice=v - np.mean(v), simple=simple, spice_secs=secs, wav=wav)
        print(f"{stage}/{name}: ngspice {secs:.1f}s, {len(v)} pts; simple {len(simple)} samples")
    return results
