"""Paths and settings for the simple-model vs SPICE validation harness."""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SPICE_DIR = os.path.join(HERE, "..")
MODELS_DIR = os.path.join(SPICE_DIR, "models")
OUT_DIR = os.path.join(HERE, "out")
EMU = os.path.join(ROOT, "emulator", "build", "emu")
NGSPICE = "ngspice"
OPAMP_MODEL = "behavioral"     # "behavioral" | "ti"  (see ../models/README.md)

FS = 48000            # audio rate of the simple model / WAV stimuli
FS_SPICE = 192000     # uniform grid ngspice output is linearised to (== emulator internal rate)
FS_PWL = 384000       # grid of the piecewise-linear stimulus fed to ngspice

# stage -> (netlist, ngspice output vector used for comparison, emu --stage flag)
STAGES = {
    "in":  ("input_stage.cir",  "v(cin)",  "in"),    # codec pin (AC side of coupling cap)
    "out": ("output_stage.cir", "v(jack)", "out"),
}
# simple-model parameter overrides applied for the *comparison* runs (codec clamp
# disabled so we compare what the board delivers, not where the model clips)
COMPARE_OVERRIDES = {"in": {"codecClipV": 1e3}, "out": {}}
CODEC_ABS_MAX_V = 1.8
DAC_FULL_SCALE_V = 1.4142   # 0 dBFS = 1 Vrms (Seed3 datasheet); must match AnalogParams::dacFullScaleV
