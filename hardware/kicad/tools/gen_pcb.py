#!/usr/bin/env python3
"""Generate pedal.kicad_pcb from tools/netlist.py with the bundled pcbnew API.

Run with KiCad's Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 tools/gen_pcb.py

Board: 150 x 98 mm, 2 layers. Top face = enclosure top: pots, toggles, LEDs, Seed3 socket,
ESP32 socket, JST footswitch headers; jacks on the rear edge (y = 0). All SMD on the top
side in strips that hold no through-hole pins. ESP32 (radio) at the far end from the
input jack (EMI, design.md §10). Footprints get the schematic symbol UUID path so
"Update PCB from Schematic" works in the GUI.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import pcbnew
from netlist import P, N, uid, pin_to_net

HERE = os.path.dirname(os.path.abspath(__file__)); PRJ = os.path.abspath(os.path.join(HERE, ".."))
FPDIR = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/"
LOCALFP = {"Daisy-Boards": os.path.join(PRJ, "lib", "Daisy-Boards.pretty"), "pedal": os.path.join(PRJ, "lib", "pedal.pretty")}
SHEET_UUID = uid("sheet-root")
W, H = 150.0, 102.0
mm = pcbnew.FromMM
def V(x, y): return pcbnew.VECTOR2I(mm(x), mm(y))

board = pcbnew.BOARD()
board.SetFileName(os.path.join(PRJ, "pedal.kicad_pcb"))
ds = board.GetDesignSettings()
ds.SetCopperLayerCount(2)

# ---- nets
nets = {}
for name in N:
    ni = pcbnew.NETINFO_ITEM(board, name); board.Add(ni); nets[name] = ni
p2n = pin_to_net()

# ---- footprints
fps = {}
def load(fpspec):
    lib, name = fpspec.split(":")
    fp = pcbnew.FootprintLoad(LOCALFP.get(lib, FPDIR + lib + ".pretty"), name)
    assert fp is not None, fpspec
    return fp
for ref, p in P.items():
    if not p["fp"]: continue
    fp = load(p["fp"]); fp.SetReference(ref); fp.SetValue(p["value"])
    fp.SetPath(pcbnew.KIID_PATH(f"/{SHEET_UUID}/{uid(f'sym:{ref}:1')}"))
    fp.SetFPIDAsString(p["fp"])
    for fname, fval in (("MPN", p["mpn"]), ("Description", p["desc"]), ("Datasheet", "")):
        fp.SetField(fname, fval)
        f = fp.GetField(fname)
        if f is not None: f.SetVisible(False)
    for pad in fp.Pads():
        key = (ref, pad.GetNumber())
        if key in p2n: pad.SetNet(nets[p2n[key]])
    board.Add(fp); fps[ref] = fp
    # pcb-side sanity: every netlist pin must exist as a pad
    padnums = {pad.GetNumber() for pad in fp.Pads()}
    for (r, pin), _ in p2n.items():
        if r == ref and pin not in padnums: raise SystemExit(f"{ref}: netlist pin {pin} not a pad of {p['fp']} (pads {sorted(padnums)})")

def place(ref, x, y, rot=0, back=False):
    fp = fps[ref]
    if back and fp.GetLayer() != pcbnew.B_Cu: fp.Flip(V(x, y), False)
    fp.SetPosition(V(x, y)); fp.SetOrientationDegrees(rot)

def crt(ref):
    fp = fps[ref]; c = fp.GetCourtyard(pcbnew.F_CrtYd)
    bb = c.BBox() if c.OutlineCount() else fp.GetBoundingBox(False, False)
    return bb.GetWidth() / 1e6, bb.GetHeight() / 1e6

def row(refs, x0, y0, xmax, rot=0, gap=0.8, back=False):
    """Flow parts left-to-right by courtyard width, wrapping at xmax. Returns bottom y."""
    x, y, rowh = x0, y0, 0.0
    for ref in refs:
        fps[ref].SetOrientationDegrees(rot)
        w, h = crt(ref)
        if x + w > xmax: x = x0; y += rowh + gap; rowh = 0.0
        place(ref, round(x + w / 2, 2), round(y + h / 2, 2), rot, back)
        x += w + gap; rowh = max(rowh, h)
    return y + rowh + gap

# ---- big parts (top face)
# jacks: ferrule/barrel protrude past the rear edge (y < 0) through the enclosure wall
# (pcbnew rotation 90 maps footprint +x to board -y, +y to board +x)
place("J2", 30.0, 6.5, 270)     # input TRS: ferrule tip y = -0.3, pads y 6.5 / 22.7, rear y 26.2, opening toward the rear edge
place("J3", 62.0, 6.5, 270)     # output TRS
place("J1", 134.0, 13.5, 270)   # DC jack: barrel tip at y = -0.5, body to y ~15.5 (clear of H2)
# pots: pins at y = 63, body extends up to y ~50.4 (Alpha 9 mm vertical, rotated 90)
for i in range(6): place(f"RV{i+1}", 12.0 + 20.0 * i, 65.0, 90)
place("SW1", 140.0, 54.0, 0); place("SW2", 140.0, 65.0, 0)
for i in range(4): place(f"D2{i+1}", 14.0 + 20.0 * i, 71.5, 0)
place("D25", 94.0, 71.5, 0)
place("U1", 24.0, 91.24, 90)    # Seed3 horizontal: pin rows y = 76 and 91.24, pads x 24..72.3, pin 1 bottom-left
place("J5", 118.0, 68.0, 0); place("J6", 133.24, 68.0, 0)   # ESP32-C3 SuperMini rows (15.24 mm apart — VERIFY on your board)
for i in range(5): place(f"J1{i+1}", 12.0 + 16.0 * i, 97.5, 0)
for i, (x, y) in enumerate([(4, 4), (W - 4, 4), (4, H - 4), (W - 4, H - 4)]): place(f"H{i+1}", x, y)

# ---- SMD / small THT strips (top face, no THT pins underneath)
inL = [f"R1{k:02d}" for k in range(1, 9)] + [f"C1{k:02d}" for k in (2, 3, 5, 6)]
inR = [f"R2{k:02d}" for k in range(1, 9)] + [f"C2{k:02d}" for k in (2, 3, 5, 6)]
outL = ["R301", "R302", "R303", "R304", "C302"]; outR = ["R401", "R402", "R403", "R404", "C402"]
# analog strips between the jacks (y < 27) and the pot bodies (y > 50): caps row, op-amp row, 0805 rows
y = row(["C101", "C104", "C201", "C204"], 3.0, 27.5, 46.0)
y = row(["U2", "C521", "U3", "C522"], 3.0, y, 46.0)
y = row(inL, 3.0, y, 46.0); row(inR, 3.0, y, 46.0)
y = row(["C301", "C303", "C401", "C403"], 48.0, 27.5, 82.0)
y = row(["U4", "C523", "U6", "C524"], 48.0, y, 82.0)
y = row(outL, 48.0, y, 82.0); row(outR, 48.0, y, 82.0)
# power strip (right, between the DC jack and the toggles/pots)
pwr_smd = ["F1", "D1", "D2", "FB1", "C506", "U5", "C507", "L1", "C508", "C509", "C510", "FB3", "R503", "C511", "FB4", "C512", "R501", "R502", "FB2", "R504", "R505", "C513", "C514"]
y = row(pwr_smd, 84.0, 24.0, 133.0)
row(["C501", "C502", "C503", "C504", "C505"], 84.0, y, 133.0)
# LED resistors next to the LEDs
row([f"R6{i}" for i in range(1, 6)], 100.0, 74.0, 112.0)

# ---- outline + mounting hole keepouts are implicit in footprints
def edge(x0, y0, x1, y1):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetStart(V(x0, y0)); s.SetEnd(V(x1, y1)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1)); board.Add(s)
edge(0, 0, W, 0); edge(W, 0, W, H); edge(W, H, 0, H); edge(0, H, 0, 0)

# ---- GND pours are created by KiCadRoutingTools route_planes.py (see route.sh)

# ---- title text
t = pcbnew.PCB_TEXT(board); t.SetText("DSP guitar pedal carrier v0.1  (GPLv3)"); t.SetPosition(V(75.0, 4.0)); t.SetLayer(pcbnew.F_SilkS); t.SetTextSize(V(1.5, 1.5)); board.Add(t)

pcbnew.SaveBoard(board.GetFileName(), board)
# report
unplaced = [r for r in fps if fps[r].GetPosition() == pcbnew.VECTOR2I(0, 0)]
print(f"pedal.kicad_pcb: {len(fps)} footprints, {len(nets)} nets; unplaced: {unplaced}")
