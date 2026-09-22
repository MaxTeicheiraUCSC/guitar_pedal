#!/usr/bin/env python3
"""Generate pedal.kicad_sch from tools/netlist.py.

Net-label style: every symbol pin gets a short wire stub and a label carrying the
net name; parts are laid out by functional group with a title per group. Multi-unit
op-amps are placed per unit. Unused pins get no-connect flags. Symbol definitions
are copied verbatim from the installed KiCad 10 libraries (extends flattened).
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from netlist import P, N, NO_CONNECT, uid, pin_to_net
from sexp import parse, find, find_all, block_text

HERE = os.path.dirname(os.path.abspath(__file__)); PRJ = os.path.abspath(os.path.join(HERE, ".."))
KSYM = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols/"
LOCAL = {"Daisy-Boards": os.path.join(PRJ, "lib", "Daisy-Boards.kicad_sym")}
GRID = 1.27
def g(v): return round(round(v / GRID) * GRID, 4)
SHEET_UUID = uid("sheet-root")
PROJECT = "pedal"

# ---------------------------------------------------------------- symbol library access
_libcache = {}
def libtext(lib):
    if lib not in _libcache:
        path = LOCAL.get(lib, KSYM + lib + ".kicad_sym"); _libcache[lib] = open(path).read()
    return _libcache[lib]

def flattened_symbol(lib, name):
    """Raw (symbol "lib:name" ...) block with `extends` resolved."""
    txt = libtext(lib)
    blk = block_text(txt, '(symbol "%s"' % name)
    m = re.search(r'\(extends "([^"]+)"\)', blk)
    if m:
        parent = m.group(1); pblk = block_text(txt, '(symbol "%s"' % parent)
        # rename parent -> child (outer and unit names), then override child's properties
        pblk = pblk.replace('(symbol "%s"' % parent, '(symbol "%s"' % name, 1)
        pblk = re.sub(r'\(symbol "%s_(\d+)_(\d+)"' % re.escape(parent), lambda mm: '(symbol "%s_%s_%s"' % (name, mm.group(1), mm.group(2)), pblk)
        cprops = {pm.group(1): pm.group(0) for pm in re.finditer(r'\(property "([^"]+)" "(?:[^"\\]|\\.)*"', blk)}
        def repl(pm):
            k = pm.group(1); return cprops.get(k, pm.group(0))
        pblk = re.sub(r'\(property "([^"]+)" "(?:[^"\\]|\\.)*"', repl, pblk)
        blk = pblk
    return blk.replace('(symbol "%s"' % name, '(symbol "%s:%s"' % (lib, name), 1)

def symbol_units(lib, name):
    """{unit: [(number, name, type, x, y, rot)]}; unit 0 pins belong to all units."""
    txt = libtext(lib); doc = parse(txt)[0]
    syms = {s[1]: s for s in find_all(doc, 'symbol')}
    s = syms[name]; ext = find(s, 'extends'); chain = [s] + ([syms[ext[1]]] if ext else [])
    units = {}
    for sym in chain:
        for u in find_all(sym, 'symbol'):
            mm = re.match(r'.*_(\d+)_(\d+)$', u[1]); unit = int(mm.group(1))
            for p in find_all(u, 'pin'):
                at = find(p, 'at'); nm = find(p, 'name'); num = find(p, 'number')
                units.setdefault(unit, []).append((num[1], nm[1], p[1], float(at[1]), float(at[2]), int(float(at[3]))))
    return units

def pins_for_unit(units, unit):
    return units.get(0, []) + units.get(unit, [])

# ---------------------------------------------------------------- layout
def outward(rot):
    return {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[rot]   # schematic coords (y down)

def cell_size(pins):
    if not pins: return 12.7, 12.7
    mx = max(abs(p[3]) for p in pins); my = max(abs(p[4]) for p in pins)
    return g(2 * mx + 2 * 2.54 + 22.86), g(2 * my + 2 * 2.54 + 7.62)

class Sheet:
    def __init__(self):
        self.out = []; self.libsyms = {}; self.pin2net = pin_to_net(); self.ncset = set(NO_CONNECT)
        self.used = set()
    def add_lib(self, lib, name):
        k = f"{lib}:{name}"
        if k not in self.libsyms: self.libsyms[k] = flattened_symbol(lib, name)
    def label(self, x, y, text, dx, dy):
        ang = {(1, 0): 0, (-1, 0): 180, (0, 1): 270, (0, -1): 90}[(dx, dy)]
        just = "left" if ang in (0, 90) else "right"
        # global labels: net names without the sheet-path prefix, so the PCB (and parity checks) see "GND", not "/GND"
        self.out.append(f'  (global_label "{text}" (shape passive) (at {x} {y} {ang}) (fields_autoplaced yes) (effects (font (size 1.27 1.27)) (justify {just})) (uuid "{uid("lbl:"+text+str((x,y)))}")\n'
                        f'    (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {x} {y} 0) (effects (font (size 1.27 1.27)) (hide yes))))')
    def wire(self, x0, y0, x1, y1):
        self.out.append(f'  (wire (pts (xy {x0} {y0}) (xy {x1} {y1})) (stroke (width 0) (type default)) (uuid "{uid("wire:"+str((x0,y0,x1,y1)))}"))')
    def text(self, x, y, s, size=2.54):
        self.out.append(f'  (text "{s}" (exclude_from_sim no) (at {x} {y} 0) (effects (font (size {size} {size}) bold) (justify left bottom)) (uuid "{uid("txt:"+s)}"))')
    def place(self, ref, ox, oy, unit=1, units=None):
        p = P[ref]; lib, name = p["sym"].split(":")
        self.add_lib(lib, name)
        units = units or symbol_units(lib, name)
        pins = pins_for_unit(units, unit)
        suuid = uid(f"sym:{ref}:{unit}")
        nunits = max(units) if units else 1
        props = [("Reference", ref, ox + 1.27, oy - 10.16, False), ("Value", p["value"], ox + 1.27, oy + 10.16, False),
                 ("Footprint", p["fp"], ox, oy, True), ("Datasheet", "~", ox, oy, True), ("Description", p["desc"], ox, oy, True), ("MPN", p["mpn"], ox, oy, True)]
        # references/values above/below the pin extent
        if pins:
            top = oy - max(pp[4] for pp in pins) - 5.08; bot = oy - min(pp[4] for pp in pins) + 5.08
            props[0] = ("Reference", ref, ox - 5.08, g(top), False); props[1] = ("Value", p["value"], ox - 5.08, g(bot), False)
        s = [f'  (symbol (lib_id "{lib}:{name}") (at {ox} {oy} 0) (unit {unit}) (exclude_from_sim no) (in_bom {"no" if p["group"] in ("flag","mech") else "yes"}) (on_board {"no" if p["group"] == "flag" else "yes"}) (dnp no)',
             f'    (uuid "{suuid}")']
        for k, v, x, y, hide in props:
            v = v.replace('"', '\\"')
            s.append(f'    (property "{k}" "{v}" (at {x} {y} 0) (effects (font (size 1.27 1.27)) (justify left){" (hide yes)" if hide else ""}))')
        for pp in pins: s.append(f'    (pin "{pp[0]}" (uuid "{uid(f"pin:{ref}:{unit}:{pp[0]}")}"))')
        s.append(f'    (instances (project "{PROJECT}" (path "/{SHEET_UUID}" (reference "{ref}") (unit {unit}))))')
        s.append('  )'); self.out.extend(s)
        for num, nm, typ, px, py, rot in pins:
            x, y = g(ox + px), g(oy - py); dx, dy = outward(rot)
            key = (ref, num)
            if key in self.pin2net:
                x1, y1 = g(x + dx * 2.54), g(y + dy * 2.54)
                self.wire(x, y, x1, y1); self.label(x1, y1, self.pin2net[key], dx, dy); self.used.add(key)
            elif key in self.ncset or typ in ("no_connect",):
                self.out.append(f'  (no_connect (at {x} {y}) (uuid "{uid(f"nc:{ref}:{num}")}"))')
            elif typ not in ("power_out",):
                # unconnected pin that is not declared: fail loudly rather than ship an open pin
                raise SystemExit(f"pin {ref}.{num} ({nm}) has no net and is not in NO_CONNECT")
            else:
                self.out.append(f'  (no_connect (at {x} {y}) (uuid "{uid(f"nc:{ref}:{num}")}"))')

def flow(sheet, refs, x0, y0, width, title, unit_map=None):
    """Place parts left-to-right in rows within `width`; returns bottom y."""
    sheet.text(x0, y0 - 2.54, title)
    x, y, rowh = x0, y0 + 5.08, 0
    for ref in refs:
        p = P[ref]; lib, name = p["sym"].split(":"); units = symbol_units(lib, name)
        unit_list = unit_map.get(ref, [1]) if unit_map else [1]
        for unit in unit_list:
            pins = pins_for_unit(units, unit); cw, ch = cell_size(pins)
            if x + cw > x0 + width: x = x0; y = g(y + rowh + 5.08); rowh = 0
            oy = g(y + ch / 2) ; ox = g(x + cw / 2)
            sheet.place(ref, ox, oy, unit, units)
            x = g(x + cw); rowh = max(rowh, ch)
    return g(y + rowh + 12.7)

def main():
    sh = Sheet()
    W = 594.0; H = 420.0   # A2
    y = 20.0
    groups = [
        ("POWER  9V in -> PTC -> Schottky -> +9V; RC-filtered +9VA for op-amps; AP63205 buck -> +5V; LC-filtered +5V_SEED / +5V_ESP; AREF = 4.5 V (U4B)",
         [r for r in P if P[r]["group"] in ("power",)] + [r for r in P if P[r]["group"] == "flag"], {}),
        ("OP-AMPS  OPA1652 x4 (OPA1602 symbol, same pinout).  U2: in L (A buffer, B inverting x-0.68)   U3: in R   U4: A out L, B AREF buffer   U6: A out R, B spare",
         [r for r in P if P[r]["group"] == "amps"], {u: [1, 2, 3] for u in ("U2", "U3", "U4", "U6")}),
        ("INPUT L  jack tip -> 10u -> 1M to AREF -> 100R/100p -> buffer -> 3k3/4k7+1n -> 10u -> 10k -> inverting (6k8||470p) -> 100R/33n -> Seed AUDIO_IN_1",
         [r for r in P if P[r]["group"] == "inL"], {}),
        ("INPUT R  (same as L) -> Seed AUDIO_IN_2", [r for r in P if P[r]["group"] == "inR"], {}),
        ("OUTPUT L  Seed AUDIO_OUT_1 -> 10u -> 15k -> inverting x-2.2 (33k||100p) -> 100R -> 10u -> jack tip, 10k load", [r for r in P if P[r]["group"] == "outL"], {}),
        ("OUTPUT R", [r for r in P if P[r]["group"] == "outR"], {}),
        ("JACKS", ["J2", "J3"], {}),
        ("CONTROLS  6 knobs (A0-A5), 2 ON-OFF-ON toggles (D5-D8), 5 footswitch headers (D0-D4), 5 LEDs (D9-D12, D26)", [r for r in P if P[r]["group"] == "ctl"], {}),
        ("ESP32-C3 SuperMini socket  (UART: Seed D13 -> IO20, Seed D14 <- IO21; 5V from +5V_ESP)", ["J5", "J6"], {}),
        ("MECHANICAL", [r for r in P if P[r]["group"] == "mech"], {}),
    ]
    x_left, width = 15.24, 400.0
    for title, refs, umap in groups:
        y = flow(sh, refs, x_left, g(y), width, title, umap)
    # Seed3 on the right
    sh.text(430.0, 17.78, "DAISY SEED3  (socketed; pins per firmware/daisy/main.cpp)")
    sh.place("U1", g(480.0), g(110.0))
    body = "\n".join(sh.out)
    libs = "\n".join(sh.libsyms.values())
    sch = f'''(kicad_sch (version 20250114) (generator "pedalgen") (generator_version "9.0")
  (uuid "{SHEET_UUID}")
  (paper "A2")
  (title_block (title "DSP guitar pedal carrier board") (rev "0.1") (company "guitar_pedal (GPLv3)") (comment 1 "Generated by hardware/kicad/tools/gen_sch.py from tools/netlist.py"))
  (lib_symbols
{libs}
  )
{body}
  (sheet_instances (path "/" (page "1")))
)
'''
    open(os.path.join(PRJ, "pedal.kicad_sch"), "w").write(sch)
    missing = [k for k in sh.pin2net if k not in sh.used]
    if missing: raise SystemExit(f"netlist pins never placed: {missing}")
    print(f"pedal.kicad_sch: {len(P)} parts, {len(sh.libsyms)} symbols, {len(N)} nets")

if __name__ == "__main__":
    main()
