# SPICE models

| file | what | status |
|---|---|---|
| `opa1652_behavioral.lib` | behavioral op-amp with OPA1652 datasheet numbers (GBW, slew, rail-to-rail limits, input C) | **substitute** — used by default |
| `opa1652_ti.lib` | TI's official macromodel | not included (licence click-through); download from ti.com/product/OPA1652 and set `OPAMP_MODEL = "ti"` in `../validate/config.py` |

The behavioral model reproduces small-signal bandwidth, slew limiting and rail
clipping, which are the effects the emulator's simple model is being checked
against. It does not model noise, offset, output current limit, or the real
open-loop phase margin, so results within ~0.1 dB / a few degrees at 20 kHz are
already at the limit of what this substitute can claim.
