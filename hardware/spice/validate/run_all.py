#!/usr/bin/env python3
"""Generate stimuli, run ngspice + the simple model, compare, write out/report.md."""
import os, sys, time
import config as C, stimuli, run_models, compare

def main():
    if not os.path.exists(C.EMU):
        sys.exit(f"build the emulator first: {C.EMU} missing (cd emulator && cmake -B build && cmake --build build)")
    os.makedirs(C.OUT_DIR, exist_ok=True)
    paths = stimuli.write_all()
    results = run_models.run_all(paths)
    report, figs, metrics = [], [], {}
    for stage in C.STAGES:
        res = {name: results[(stage, name)] for (st, name) in results if st == stage}
        metrics[stage] = compare.analyse(stage, res, report, figs)
    spice_secs = sum(r["spice_secs"] for r in results.values())
    path = compare.write_report(metrics, report, figs, spice_secs)
    print(open(path).read())

if __name__ == "__main__":
    main()
