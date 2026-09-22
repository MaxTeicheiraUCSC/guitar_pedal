"""Metrics comparing the simple model against the ngspice reference, and the report."""
import os, json
import numpy as np
from scipy.signal import resample_poly, correlate
from scipy.io import wavfile
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import config as C

def to_audio_rate(v):
    """ngspice 192 kHz grid -> 48 kHz, band-limited (this stands in for the codec's decimator)."""
    return resample_poly(v, 1, C.FS_SPICE // C.FS)

def align(ref, y, max_lag=200):
    """Integer-lag alignment of y to ref (simple model has a fixed FIR latency)."""
    n = min(len(ref), len(y))
    c = correlate(y[:n], ref[:n], mode="full")
    mid = n - 1
    lags = np.arange(-max_lag, max_lag + 1)
    lag = lags[np.argmax(c[mid - max_lag: mid + max_lag + 1])]
    if lag > 0: y2 = y[lag:]; r2 = ref[: len(y2)]
    else:       r2 = ref[-lag:]; y2 = y[: len(r2)]
    n = min(len(r2), len(y2))
    return r2[:n], y2[:n], int(lag)

def transfer(x, y, fs, nfft=1 << 15):
    X = np.fft.rfft(x, nfft); Y = np.fft.rfft(y, nfft)
    f = np.fft.rfftfreq(nfft, 1 / fs)
    H = Y / np.where(np.abs(X) < 1e-9, 1e-9, X)
    return f, H

def band_smooth(f, H, fmin=20, fmax=20000, per_oct=12):
    edges = fmin * 2 ** (np.arange(0, np.log2(fmax / fmin) * per_oct + 1) / per_oct)
    fc, mag, ph = [], [], []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (f >= lo) & (f < hi)
        if not m.any(): continue
        fc.append(np.sqrt(lo * hi)); mag.append(20 * np.log10(np.mean(np.abs(H[m])))); ph.append(np.degrees(np.angle(np.mean(H[m]))))
    return np.array(fc), np.array(mag), np.array(ph)

def thd(seg, f0, fs, nh=10):
    n = len(seg); w = np.hanning(n); S = np.abs(np.fft.rfft(seg * w)); fr = np.fft.rfftfreq(n, 1 / fs)
    def peak(fh):
        i = int(round(fh / (fs / n))); lo, hi = max(i - 3, 0), min(i + 4, len(S)); return S[lo:hi].max()
    fund = peak(f0); harm = np.sqrt(sum(peak(k * f0) ** 2 for k in range(2, nh + 1) if k * f0 < fs / 2))
    return 100 * harm / fund

def analyse(stage, res, report, figs):
    """res: dict name -> {spice, simple, ...}. Appends lines to report, saves figures."""
    fs = C.FS
    # ---------- sweep: magnitude / phase ----------
    r = res["sweep"]; x = wavfile.read(r["wav"])[1].astype(np.float64)
    sp = to_audio_rate(r["spice"]); si = r["simple"]
    sp1, si2, lag = align(sp, si)            # both stages invert: align the two model outputs to each other
    n = min(len(x), len(sp1)); f, Hs = transfer(x[:n], sp1[:n], fs); _, Hm = transfer(x[:n], si2[:n], fs)
    fc, ms, ps = band_smooth(f, Hs); _, mm, pm = band_smooth(f, Hm)
    dmag = mm - ms; dph = (pm - ps + 180) % 360 - 180
    g1k = np.interp(1000, fc, ms), np.interp(1000, fc, mm)
    report.append(f"### {stage}: frequency response (20 Hz – 20 kHz, 1/12-oct bands)\n")
    report.append(f"- gain @1 kHz: SPICE {g1k[0]:+.3f} dB, simple {g1k[1]:+.3f} dB")
    report.append(f"- magnitude error: max {np.max(np.abs(dmag)):.3f} dB (at {fc[np.argmax(np.abs(dmag))]:.0f} Hz), rms {np.sqrt(np.mean(dmag**2)):.3f} dB")
    report.append(f"- phase error: max {np.max(np.abs(dph)):.2f}° (at {fc[np.argmax(np.abs(dph))]:.0f} Hz)")
    report.append(f"- simple-model latency vs SPICE: {lag} samples @48 kHz\n")
    fig, ax = plt.subplots(2, 1, figsize=(8, 6), sharex=True)
    ax[0].semilogx(fc, ms, label="ngspice"); ax[0].semilogx(fc, mm, "--", label="simple model"); ax[0].set_ylabel("dB"); ax[0].legend(); ax[0].grid(True, which="both", alpha=.3)
    ax[0].set_title(f"{stage} stage: transfer function")
    ax[1].semilogx(fc, dmag, label="mag err dB"); ax[1].semilogx(fc, dph / 10, label="phase err / 10 deg"); ax[1].legend(); ax[1].grid(True, which="both", alpha=.3); ax[1].set_xlabel("Hz")
    p = os.path.join(C.OUT_DIR, f"{stage}_response.png"); fig.savefig(p, dpi=110); plt.close(fig); figs.append(p)
    metrics = dict(mag_err_max_db=float(np.max(np.abs(dmag))), phase_err_max_deg=float(np.max(np.abs(dph))), gain_1k_spice_db=float(g1k[0]), gain_1k_simple_db=float(g1k[1]))

    # ---------- level steps: THD, compression, codec over-range ----------
    r = res["steps"]; x = wavfile.read(r["wav"])[1].astype(np.float64)
    sp = to_audio_rate(r["spice"]); si = r["simple"]
    sp1, si2, _ = align(sp, si)
    seg = int(0.025 * fs); nseg = len(x) // seg
    rows = []; lvl_names = []
    for k in range(nseg):
        a, b = k * seg + int(0.005 * fs), (k + 1) * seg - int(0.005 * fs)
        xin = x[a:b]; A = np.abs(xin).max()
        if b > min(len(sp1), len(si2)): break
        ys, ym = sp1[a:b] - sp1[a:b].mean(), si2[a:b] - si2[a:b].mean()   # per-segment DC removal (coupling caps drift when clipping)
        rows.append((A, np.abs(ys).max(), np.abs(ym).max(), thd(ys, 1000, fs), thd(ym, 1000, fs), 20 * np.log10(np.abs(ys).max() / A), 20 * np.log10(np.abs(ym).max() / A)))
    ref = min(rows, key=lambda r_: abs(r_[0] - (1.0 if stage == "in" else 0.5)))   # reference gain at a mid level
    report.append(f"### {stage}: 1 kHz level steps\n")
    unit = "V at jack" if stage == "in" else "× full scale (DAC)"
    report.append(f"| input pk ({unit}) | SPICE pk V | simple pk V | SPICE THD % | simple THD % | SPICE gain dB | simple gain dB |")
    report.append("|---|---|---|---|---|---|---|")
    onset_s = onset_m = None; over = None
    for A, ps_, pm_, ts, tm, gs, gm in rows:
        report.append(f"| {A:.2f} | {ps_:.3f} | {pm_:.3f} | {ts:.3f} | {tm:.3f} | {gs:+.2f} | {gm:+.2f} |")
        if onset_s is None and gs < ref[5] - 1.0: onset_s = A
        if onset_m is None and gm < ref[6] - 1.0: onset_m = A
        if stage == "in" and over is None and ps_ > C.CODEC_ABS_MAX_V: over = A
    report.append("")
    report.append(f"- 1 dB compression onset: SPICE {onset_s if onset_s else '> max'} , simple {onset_m if onset_m else '> max'} ({unit})")
    if stage == "in":
        report.append(f"- codec pin exceeds ±{C.CODEC_ABS_MAX_V} V (abs max) from input peak: **{over if over else '> 4.0'} V** at the jack (SPICE)")
    report.append("")
    fig, ax = plt.subplots(1, 2, figsize=(10, 4))
    A = [r_[0] for r_ in rows]
    ax[0].plot(A, [r_[1] for r_ in rows], "o-", label="ngspice"); ax[0].plot(A, [r_[2] for r_ in rows], "s--", label="simple"); ax[0].set_xlabel(f"input peak ({unit})"); ax[0].set_ylabel("output peak V"); ax[0].legend(); ax[0].grid(alpha=.3)
    if stage == "in": ax[0].axhline(C.CODEC_ABS_MAX_V, color="r", ls=":", label="codec abs max")
    ax[1].semilogy(A, [max(r_[3], 1e-3) for r_ in rows], "o-", label="ngspice"); ax[1].semilogy(A, [max(r_[4], 1e-3) for r_ in rows], "s--", label="simple"); ax[1].set_xlabel(f"input peak ({unit})"); ax[1].set_ylabel("THD %"); ax[1].legend(); ax[1].grid(alpha=.3)
    fig.suptitle(f"{stage} stage: level steps"); p = os.path.join(C.OUT_DIR, f"{stage}_levels.png"); fig.savefig(p, dpi=110); plt.close(fig); figs.append(p)
    metrics.update(dict(onset_spice=onset_s, onset_simple=onset_m, codec_over_range_from=over, steps=[[float(v) for v in r_] for r_ in rows]))

    # ---------- waveform error: square + pluck (+ clipped step) ----------
    report.append(f"### {stage}: waveform match (after alignment, normalised RMS error)\n")
    for name in ("square", "pluck", "steps"):
        r = res[name]; sp = to_audio_rate(r["spice"]); si = r["simple"]
        a, b, _ = align(sp, si)
        err = np.sqrt(np.mean((a - b) ** 2)) / np.sqrt(np.mean(a ** 2))
        report.append(f"- {name}: NRMSE {100 * err:.2f} %")
        metrics[f"nrmse_{name}"] = float(err)
        if name in ("square", "steps"):
            fig, ax = plt.subplots(figsize=(10, 3.5))
            if name == "square": s0, s1 = 0, int(0.012 * fs)
            else: s0 = int(0.025 * fs) * (len(rows) - 1) + int(0.008 * fs); s1 = s0 + int(0.004 * fs)
            t = np.arange(s0, s1) / fs * 1e3
            ax.plot(t, a[s0:s1], label="ngspice"); ax.plot(t, b[s0:s1], "--", label="simple"); ax.set_xlabel("ms"); ax.set_ylabel("V"); ax.legend(); ax.grid(alpha=.3)
            ax.set_title(f"{stage} stage: {name}" + (" (largest level)" if name == "steps" else ""))
            p = os.path.join(C.OUT_DIR, f"{stage}_{name}_wave.png"); fig.savefig(p, dpi=110); plt.close(fig); figs.append(p)
    report.append("")
    return metrics

def write_report(all_metrics, report_lines, figs, spice_secs):
    lines = ["# Simple analog model vs ngspice reference\n",
             f"Op-amp model: `{C.OPAMP_MODEL}` (see `../models/README.md`). ngspice total {spice_secs:.0f} s. "
             f"SPICE output linearised to {C.FS_SPICE} Hz and decimated to {C.FS} Hz for comparison; the simple model runs at {C.FS} Hz with 8× internal oversampling.\n",
             "Pass criteria from docs/design.md §12: |mag err| < 0.5 dB and |phase err| < 5° over 20 Hz–20 kHz; clip onset within 0.5 dB.\n"]
    for st, m in all_metrics.items():
        ok_mag = m["mag_err_max_db"] < 0.5; ok_ph = m["phase_err_max_deg"] < 5
        on_s, on_m = m["onset_spice"], m["onset_simple"]
        ok_on = (on_s is None and on_m is None) or (on_s and on_m and abs(20 * np.log10(on_m / on_s)) < 0.5)
        lines.append(f"- **{st} stage**: magnitude {'PASS' if ok_mag else 'FAIL'} ({m['mag_err_max_db']:.3f} dB), phase {'PASS' if ok_ph else 'FAIL'} ({m['phase_err_max_deg']:.2f}°), clip onset {'PASS' if ok_on else 'FAIL'} (SPICE {on_s}, simple {on_m})")
    lines.append("")
    lines += report_lines
    lines.append("## Figures\n")
    for p in figs: lines.append(f"![{os.path.basename(p)}]({os.path.basename(p)})\n")
    lines.append("## Assumptions carried into both models\n")
    lines.append("- AREF_AUDIO_BIAS = 4.5 V, stiff. Codec input: AC coupled (4.7 µF assumed), 20 kΩ, no clamp on the board as drawn.")
    lines.append("- Op-amp: behavioral OPA1652 substitute unless `OPAMP_MODEL = \"ti\"`.")
    lines.append("- Comparison is of the AC signal at the codec pin with the simple model's codec clamp disabled; the over-range row above is the design check for whether clamp diodes are needed.")
    path = os.path.join(C.OUT_DIR, "report.md"); open(path, "w").write("\n".join(lines))
    json.dump(all_metrics, open(os.path.join(C.OUT_DIR, "metrics.json"), "w"), indent=1, default=float)
    return path
