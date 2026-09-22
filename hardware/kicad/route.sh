#!/bin/bash
# Route pedal.kicad_pcb with KiCadRoutingTools (drandyhaas), producing routed/pedal_routed.kicad_pcb.
# Sequence per the plugin's plan-pcb-routing skill: pour GND (B.Cu) -> route all nets -> verify.
set -e
RT=${RT:-$HOME/Documents/KiCad/10.0/3rdparty/plugins/com_github_drandyhaas_kicadroutingtools}
KC=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
HERE=$(cd "$(dirname "$0")" && pwd); cd "$HERE"
rm -rf routed && mkdir -p routed
python3 "$RT/py_router/copy_board.py" pedal.kicad_pcb routed/s0.kicad_pcb        # copies the .kicad_pro (DRC floor) too
cd "$RT"
python3 -X utf8 py_router/route_planes.py "$HERE/routed/s0.kicad_pcb" "$HERE/routed/s1.kicad_pcb" \
    --nets GND --plane-layers B.Cu 2>&1 | tee "$HERE/routed/step1_pour.log" | tail -5
python3 -X utf8 py_router/route.py "$HERE/routed/s1.kicad_pcb" "$HERE/routed/s2.kicad_pcb" \
    --nets "*" --max-ripup 5 \
    --power-nets GND VIN_RAW VIN_F VIN_D +9V +9V_RC1 +9VA +9VA_OPA +5V +5V_FB +5V_SEED +5V_ESP BUCK_SW \
    --power-nets-widths 0.6 0.8 0.8 0.8 0.8 0.6 0.6 0.5 0.8 0.6 0.6 0.6 0.8 \
    --layers F.Cu B.Cu --layer-costs 1.0 3.0 2>&1 | tee "$HERE/routed/step2_route.log" | tail -15
python3 -X utf8 py_router/check_drc.py "$HERE/routed/s2.kicad_pcb" 2>&1 | tee "$HERE/routed/check_drc.log" | tail -8
python3 -X utf8 py_router/check_connected.py "$HERE/routed/s2.kicad_pcb" 2>&1 | tee "$HERE/routed/check_connected.log" | tail -8
cd "$HERE"
python3 "$RT/py_router/copy_board.py" routed/s2.kicad_pcb routed/pedal_routed.kicad_pcb
$KC pcb drc --output routed/kicad_drc.rpt --severity-error --severity-warning routed/pedal_routed.kicad_pcb | tail -3
grep -c "^\[" routed/kicad_drc.rpt || true
