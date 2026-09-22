#!/usr/bin/env python3
"""Post-route touch-ups on a routed board (run with KiCad's python): add the MPN/Description
fields from netlist.py to every footprint and shrink reference text on small parts."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import pcbnew
from netlist import P, NO_CONNECT
from gen_sch import symbol_units
path = sys.argv[1]
SEED3_MODEL_OFFSET = (0.0, 0.0, 0.0)   # mm, tune after viewing
SEED3_MODEL_ROT_Z = 0.0
b = pcbnew.LoadBoard(path)
for fp in b.GetFootprints():
    p = P.get(fp.GetReference())
    if not p: continue
    for fname, fval in (("MPN", p["mpn"]), ("Description", p["desc"]), ("Datasheet", "")):
        fp.SetField(fname, fval)
        f = fp.GetField(fname)
        if f is not None: f.SetVisible(False)
    if any(k in p["fp"] for k in ("0805", "0603", "1206", "SOT-23", "SOD-323", "SMA")):
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(0.8), pcbnew.FromMM(0.8))); fp.Reference().SetTextThickness(pcbnew.FromMM(0.12))
        fp.Value().SetVisible(False)
pinname = {}
for ref, num in NO_CONNECT:
    lib, name = P[ref]["sym"].split(":")
    for u, pins in symbol_units(lib, name).items():
        for pp in pins:
            if pp[0] == num: pinname[(ref, num)] = pp[1]
for fp in b.GetFootprints():
    for pad in fp.Pads():
        key = (fp.GetReference(), pad.GetNumber())
        if key in pinname and pad.GetNetname() == "":
            nm = f"unconnected-({key[0]}-{pinname[key]}-Pad{key[1]})" if pinname[key] else f"unconnected-({key[0]}-Pad{key[1]})"
            ni = b.FindNet(nm) or pcbnew.NETINFO_ITEM(b, nm)
            if b.FindNet(nm) is None: b.Add(ni)
            pad.SetNet(ni)
# Seed3 3D model from Electro-Smith (Seed3-models.zip); offset/rotation checked visually in the 3D viewer
for fp in b.GetFootprints():
    if fp.GetReference() == "U1":
        ms = fp.Models(); ms.clear()
        m = pcbnew.FP_3DMODEL(); m.m_Filename = "${KIPRJMOD}/lib/3d/ES_Daisy_Seed3_Rev5.step"
        m.m_Offset.x, m.m_Offset.y, m.m_Offset.z = SEED3_MODEL_OFFSET; m.m_Rotation.z = SEED3_MODEL_ROT_Z
        ms.push_back(m)
pcbnew.SaveBoard(path, b)
print("finalized", path)
