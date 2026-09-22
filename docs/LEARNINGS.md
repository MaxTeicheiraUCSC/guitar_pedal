# Learnings

Non-obvious things this project cost time to find. Each one is a real bug or fact that was
caught by a check, not by reasoning — which is the argument for keeping the checks.

## Circuit / DSP

**The input-stage gain was wrong by 1.6 dB, and only SPICE caught it.** The hand-derived
model said the 3k3/4k7 divider gives 0.5875. It does not: the second stage's 10 kΩ input
resistor loads the 4k7 leg, so it is 3k3/(4k7‖10k) = 0.492 (−6.2 dB). Found by comparing the
emulator's simple model against an ngspice run of the same schematic.

**The codec's ±1.8 V is both 0 dBFS and the absolute-maximum rating.** So "add clamp diodes for
headroom" was wrong twice over: clamps do not add headroom, and the codec node sits at the 4.5 V
AREF bias, so diodes to GND/+3V3 would clamp at the wrong levels anyway. The fix was gain
structure — second stage 6k8/10k = 0.68, so the op-amp hits its rails (±4.4 V × 0.492 × 0.68 =
1.48 V) before the codec sees its limit.

**Three of the four coupling capacitors per channel were drawn backwards.** Polarised
electrolytics must face the node with the higher DC level; checking each cap against the DC
level on both sides is a distinct review pass from checking connectivity.

**Bypass with trails was attenuating the dry signal.** A bypassed delay/reverb still ran its
`(1 − mix)` dry gain while the tail decayed, so bypassing ducked the dry path; with feedback > 1
the tail never decayed, making it permanent. Dry now passes at unity when bypassed, and
feedback/decay are capped below 1 while bypassed. This would have shipped into the pedal
firmware too — the emulator caught it.

**Oversampler latency is not (taps−1)/2 per stage.** For a matched up/down pair the impulse
lands at exactly `kTapsPerPhase − 1` input samples; derive it by measuring the impulse, not by
algebra on the FIR length.

**4× oversampling + ADAA moved the fuzz's non-harmonic energy from −28 dB to −42 dB.** Worth the
CPU: the fuzz costs ~1.6 % of real time on an M4 Pro versus <0.3 % for the other three effects
combined, and is the block most at risk on the Cortex-M7.

## Toolchain and platform

**A JUCE standalone that "crashes" on macOS may be missing `NSMicrophoneUsageDescription`.**
macOS kills any app that opens the audio input without it. It survives when launched from a
terminal (inherits that terminal's permission) and dies when launched from Finder — which looks
exactly like an intermittent crash.

**"It crashed" was a hang.** A 30 Hz full-window `repaint()` on a Retina display burned 88 % CPU
and starved the UI thread. Check whether the process is still alive before debugging a crash:
still running = hang, and the absence of a report in `~/Library/Logs/DiagnosticReports` is
evidence, not an accident.

**ASan hangs at startup on macOS 26.** Its shadow-memory init never returns. Use lldb plus an
in-process stress harness instead (`emulator/juce/Harness.cpp`).

**pluginval forks a child process by default**, so a shell wrapper reads an empty exit code and
reports failure on a passing plugin. Use `--validate-in-process` and read the exit code from
`Start-Process -PassThru`.

**Windows packaging (the defect Roman hit).** A JUCE plugin built with the default `/MD` runtime
imports `MSVCP140.dll` / `VCRUNTIME140*.dll` and will not load on a machine without the Visual
C++ 2015-2022 Redistributable — the DAW reports only "import failed". Build with
`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` and enforce it (`tools/check_pe_imports.py`, pure
stdlib PE parser, runs on any OS). Related: a VST3 is a *folder* bundle; dragging it into a DAW
never installs anything, and dragging out of Windows' zip preview copies an incomplete folder.
Ship an installer.

**MSVC leaves struct padding uninitialised**, so a byte-for-byte `memcmp` of a preset struct
fails on Windows while passing on clang. Compare fields.

## KiCad

**KiCad 10 library files are deeply nested s-expressions.** Regex extraction of pins/symbols
silently returns nothing or the wrong block; write a real parser (`hardware/kicad/tools/sexp.py`).

**Local labels get a sheet-path prefix (`/GND`) in the exported netlist**, which fails
`--schematic-parity` against the board's `GND`. Use global labels in a generated flat schematic.

**Generate the schematic and the PCB from one netlist description**, then gate on
`kicad-cli sch export netlist` matching that description pad-for-pad (`tools/check_netlist.py`).
That check is what makes a script-generated board trustworthy.

**Electro-Smith publishes an official KiCad symbol/footprint** (DaisyKiCad) and the Seed3 STEP
model in a separate `Seed3-models.zip`. Several stock KiCad footprints have **no** 3D model
anywhere upstream — Neutrik NMJ6HCD2, Alpha RD901F, Fuse_1812 — so an empty render is expected,
not a broken install.

**KiCadRoutingTools rules worth obeying:** never copy a `.kicad_pcb` without its sibling
`.kicad_pro` (it carries the DRC floor), route to a fresh output path each run, and grade with
both `check_drc.py` *and* `check_connected.py` — a DRC-clean board can be entirely disconnected.

## Agent-safety plumbing

**A text-pattern guard on Bash produces false positives that block real work.** Heredoc bodies
and quoted multi-line commit messages were parsed as command words; `git remote -v` and
`git config --get` are read-only but matched a mutation rule. Each was found by the guard
blocking a legitimate action — hence `.claude/hooks/test_guard.py` (64 allow/deny cases) so the
rules can be changed without re-breaking the last fix.

**The guard is enforcement; the prompt is policy.** Instructions telling a model to ignore
injected email are only as good as the model's compliance. A `PreToolUse` hook that inspects the
actual tool arguments — recipient, body, path, command — is what makes "it cannot read outside
the repo" a statement about the system rather than about intentions.
