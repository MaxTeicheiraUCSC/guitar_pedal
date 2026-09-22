"""Single source of truth for the carrier board: parts and nets.

Everything else (schematic, PCB, BOM, pin map) is generated from this file.
Pin numbers are the KiCad library symbol pin numbers (verified against the
installed KiCad 10 libraries with tools/sexp.py); pad names match them.

Circuit references:
  - Seed3 datasheet Fig 1.1 (+9 V input filtering), Fig 3.4 (instrument input),
    Fig 3.5 (instrument output); AREF = 4.5 V buffered.
  - hardware/spice/validate: codec pin reaches ±1.8 V before the op-amps clip.
    The codec node sits at the 4.5 V AREF bias, so diode clamps to GND/3V3 would
    clamp at the wrong levels; instead the second stage gain is 6k8/10k = 0.68 so
    the op-amp's own rail limit (±4.4 V) x 0.492 x 0.68 = 1.47 V < 1.8 V. C5 = 470 pF
    keeps the feedback corner at ~50 kHz.
  - AP63205 datasheet (DS41326): 10 µF in, 4.7 µH, 2x22 µF out, 100 nF BST, FB->VOUT (fixed 5 V).
  - firmware/daisy/main.cpp pin map (knobs A0-A5, switches D0-D4, toggles D5-D8,
    LEDs D9-D12 + D26, USART1 D13/D14). The input-jack ring switch (D27) was dropped:
    a TS plug grounds the ring, so the firmware detects mono from a silent R input.
"""
import uuid
from collections import OrderedDict

NAMESPACE = uuid.UUID("6d1f5b8e-4c2a-4b7e-9a0f-2f0e1c3d4a5b")
def uid(key): return str(uuid.uuid5(NAMESPACE, key))

# ---------------------------------------------------------------- parts
# ref -> dict(sym="Lib:Name", fp="Lib:Name", value, mpn, desc, group)
P = OrderedDict()
def part(ref, sym, fp, value, mpn="", desc="", group="misc", **kw):
    P[ref] = dict(ref=ref, sym=sym, fp=fp, value=value, mpn=mpn, desc=desc, group=group, **kw)

# --- boards / modules
part("U1", "Daisy-Boards:Daisy_Seed", "Daisy-Boards:DAISY_SEED", "Daisy Seed3", "Electro-Smith Seed3", "DSP board, socketed (2x 1x20 female headers)", "seed")
part("J5", "Connector_Generic:Conn_01x08", "Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical", "ESP32-C3 SuperMini A", "", "left row: 5V GND 3V3 IO0 IO1 IO2 IO3 IO4", "esp")
part("J6", "Connector_Generic:Conn_01x08", "Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical", "ESP32-C3 SuperMini B", "", "right row: IO5 IO6 IO7 IO8 IO9 IO10 IO21 IO20", "esp")
# --- jacks
part("J1", "Connector:Barrel_Jack_Switch", "Connector_BarrelJack:BarrelJack_Horizontal", "DC 9V centre-neg", "CUI PJ-102AH or Lumberg 1613 09", "2.1 mm barrel; pedal standard: sleeve = +9 V, centre = 0 V", "power")
part("J2", "Connector_Audio:AudioJack3_SwitchTR", "Connector_Audio:Jack_6.35mm_Neutrik_NMJ6HCD2_Horizontal", "IN TRS", "Neutrik NMJ6HCD2", "input, TS = mono", "in")
part("J3", "Connector_Audio:AudioJack3_SwitchTR", "Connector_Audio:Jack_6.35mm_Neutrik_NMJ6HCD2_Horizontal", "OUT TRS", "Neutrik NMJ6HCD2", "output", "out")
# --- power section (Seed3 datasheet Fig 1.1 + AP63205 buck)
part("F1", "Device:Polyfuse", "Fuse:Fuse_1812_4532Metric", "1A PTC", "Bourns MF-MSMF110-2", "resettable fuse", "power")
part("D1", "Device:D_Schottky", "Diode_SMD:D_SOD-323", "NSR1020MW2", "onsemi NSR1020MW2T3G", "reverse polarity, SOD-323", "power")
part("D2", "Device:D_TVS", "Diode_SMD:D_SMA", "SMAJ12A", "Littelfuse SMAJ12A", "surge clamp", "power")
part("FB1", "Device:FerriteBead", "Inductor_SMD:L_0805_2012Metric", "220R@100MHz", "Murata BLM21PG221SN1D", "input bead", "power")
part("FB2", "Device:FerriteBead", "Inductor_SMD:L_0805_2012Metric", "220R@100MHz", "Murata BLM21PG221SN1D", "analog rail bead", "power")
part("FB3", "Device:FerriteBead", "Inductor_SMD:L_0805_2012Metric", "220R@100MHz", "Murata BLM21PG221SN1D", "Seed 5V bead", "power")
part("FB4", "Device:FerriteBead", "Inductor_SMD:L_0805_2012Metric", "220R@100MHz", "Murata BLM21PG221SN1D", "ESP32 5V bead", "power")
for r in ("R501", "R502", "R503"): part(r, "Device:R", "Resistor_SMD:R_1206_3216Metric", "3R3", "", "rail RC filter", "power")
for c in ("C501", "C502", "C503", "C504", "C505"): part(c, "Device:C_Polarized", "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm", "100u/16V", "", "rail bulk", "power")
part("C506", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "100n", "", "+9V hf", "power")
part("U5", "Regulator_Switching:AP63205WU", "Package_TO_SOT_SMD:TSOT-23-6", "AP63205WU-7", "Diodes AP63205WU-7", "9V->5V buck 2A", "power")
part("L1", "Device:L", "Inductor_SMD:L_Bourns_SRN6045TA", "4u7", "Bourns SRN6045TA-4R7M", "buck inductor", "power")
part("C507", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "10u/25V", "", "buck Cin", "power")
part("C508", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "22u/10V", "", "buck Cout", "power")
part("C509", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "22u/10V", "", "buck Cout", "power")
part("C510", "Device:C", "Capacitor_SMD:C_0603_1608Metric", "100n", "", "buck BST", "power")
part("C511", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "100n", "", "+5V_SEED hf", "power")
part("C512", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "100n", "", "+5V_ESP hf", "power")
# AREF 4.5 V: divider + buffer (U4B)
part("R504", "Device:R", "Resistor_SMD:R_0805_2012Metric", "10k", "", "AREF divider", "power")
part("R505", "Device:R", "Resistor_SMD:R_0805_2012Metric", "10k", "", "AREF divider", "power")
part("C513", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "10u", "", "AREF divider filter", "power")
part("C514", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "100n", "", "AREF out hf", "power")
# --- op-amps: OPA1652 (dual, SOIC-8). The OPA1602 symbol has the standard dual pinout.
for u, d in (("U2", "input L: A buffer, B inverting"), ("U3", "input R: A buffer, B inverting"), ("U4", "A output L, B AREF buffer"), ("U6", "A output R, B spare follower")):
    part(u, "Amplifier_Operational:OPA1602", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "OPA1652AIDR", "TI OPA1652AIDR", d, "amps")
for c, u in (("C521", "U2"), ("C522", "U3"), ("C523", "U4"), ("C524", "U6")): part(c, "Device:C", "Capacitor_SMD:C_0805_2012Metric", "100n", "", f"{u} decoupling", "amps")
# --- audio channels (Fig 3.4 input, Fig 3.5 output); 1xx = in L, 2xx = in R, 3xx = out L, 4xx = out R
def input_chain(n, ch):
    part(f"C{n}01", "Device:C_Polarized", "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm", "10u/50V", "", f"in {ch} coupling", f"in{ch}")
    part(f"R{n}01", "Device:R", "Resistor_SMD:R_0805_2012Metric", "1M", "", f"in {ch} bias", f"in{ch}")
    part(f"R{n}02", "Device:R", "Resistor_SMD:R_0805_2012Metric", "10k", "", f"in {ch} tip-normal pull-down", f"in{ch}")
    part(f"R{n}03", "Device:R", "Resistor_SMD:R_0805_2012Metric", "100R", "", f"in {ch} rf", f"in{ch}")
    part(f"C{n}02", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "100p", "", f"in {ch} rf", f"in{ch}")
    part(f"R{n}04", "Device:R", "Resistor_SMD:R_0805_2012Metric", "3k3", "", f"in {ch} divider", f"in{ch}")
    part(f"C{n}03", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "1n", "", f"in {ch} divider lp", f"in{ch}")
    part(f"R{n}05", "Device:R", "Resistor_SMD:R_0805_2012Metric", "4k7", "", f"in {ch} divider", f"in{ch}")
    part(f"C{n}04", "Device:C_Polarized", "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm", "10u/50V", "", f"in {ch} stage-2 coupling", f"in{ch}")
    part(f"R{n}06", "Device:R", "Resistor_SMD:R_0805_2012Metric", "10k", "", f"in {ch} stage-2 in", f"in{ch}")
    part(f"R{n}07", "Device:R", "Resistor_SMD:R_0805_2012Metric", "6k8", "", f"in {ch} stage-2 fb (gain 0.68: codec never exceeds 1.8 V)", f"in{ch}")
    part(f"C{n}05", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "470p", "", f"in {ch} stage-2 fb (~50 kHz)", f"in{ch}")
    part(f"R{n}08", "Device:R", "Resistor_SMD:R_0805_2012Metric", "100R", "", f"in {ch} codec series", f"in{ch}")
    part(f"C{n}06", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "33n", "", f"in {ch} codec lp", f"in{ch}")
def output_chain(n, ch):
    part(f"C{n}01", "Device:C_Polarized", "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm", "10u/50V", "", f"out {ch} DAC coupling", f"out{ch}")
    part(f"R{n}01", "Device:R", "Resistor_SMD:R_0805_2012Metric", "15k", "", f"out {ch} in", f"out{ch}")
    part(f"R{n}02", "Device:R", "Resistor_SMD:R_0805_2012Metric", "33k", "", f"out {ch} fb", f"out{ch}")
    part(f"C{n}02", "Device:C", "Capacitor_SMD:C_0805_2012Metric", "100p", "", f"out {ch} fb", f"out{ch}")
    part(f"R{n}03", "Device:R", "Resistor_SMD:R_0805_2012Metric", "100R", "", f"out {ch} series", f"out{ch}")
    part(f"C{n}03", "Device:C_Polarized", "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm", "10u/50V", "", f"out {ch} coupling", f"out{ch}")
    part(f"R{n}04", "Device:R", "Resistor_SMD:R_0805_2012Metric", "10k", "", f"out {ch} load", f"out{ch}")
input_chain(1, "L"); input_chain(2, "R"); output_chain(3, "L"); output_chain(4, "R")
# --- controls
for i in range(1, 7): part(f"RV{i}", "Device:R_Potentiometer", "Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical", "B10k", "Alpha RD901F-40-15F-B10K", f"knob {i}", "ctl")
part("SW1", "Switch:SW_SPDT", "pedal:SW_Toggle_2M_SPDT_Vertical", "HP cut ON-OFF-ON", "Dailywell 2MS3T1B1M2QES", "global high-cut toggle", "ctl")
part("SW2", "Switch:SW_SPDT", "pedal:SW_Toggle_2M_SPDT_Vertical", "LP cut ON-OFF-ON", "Dailywell 2MS3T1B1M2QES", "global low-cut toggle", "ctl")
for i, nm in enumerate(("FS Tremolo", "FS Delay", "FS Reverb", "FS Fuzz", "FS TAP"), 1):
    part(f"J1{i}", "Connector_Generic:Conn_01x02", "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical", nm, "JST B2B-XH-A", "to SPST momentary footswitch", "ctl")
for i, nm in enumerate(("LED Tremolo", "LED Delay", "LED Reverb", "LED Fuzz", "LED Tempo"), 1):
    part(f"D2{i}", "Device:LED", "LED_THT:LED_D3.0mm", nm, "", "3 mm", "ctl")
    part(f"R6{i}", "Device:R", "Resistor_SMD:R_0805_2012Metric", "1k", "", f"{nm} series", "ctl")
for i in range(1, 5): part(f"H{i}", "Mechanical:MountingHole", "MountingHole:MountingHole_3.2mm_M3", "M3", "", "", "mech")
for i, n in enumerate(("+9V", "+9VA", "+5V", "+5V_SEED", "+5V_ESP", "+9VA_OPA", "VIN_RAW", "GND"), 1):
    part(f"PF{i}", "power:PWR_FLAG", "", n, "", "", "flag")

# ---------------------------------------------------------------- nets
N = OrderedDict()
def net(name, *conns):
    N.setdefault(name, [])
    for ref, pin in conns:
        assert ref in P, ref
        N[name].append((ref, str(pin)))

# power path: jack sleeve (+9) -> PTC -> Schottky -> bead -> +9V
net("VIN_RAW", ("J1", "2"), ("F1", "1"), ("PF7", "1"))
net("VIN_F", ("F1", "2"), ("D1", "2"))              # D1 anode
net("VIN_D", ("D1", "1"), ("FB1", "1"), ("D2", "2"))  # D1 cathode; TVS across it
net("+9V", ("FB1", "2"), ("C501", "1"), ("C506", "1"), ("R501", "1"), ("U5", "3"), ("U5", "2"), ("C507", "1"), ("PF1", "1"))
# analog rail: +9V -> 3R3 -> 100u -> 3R3 -> 100u = +9VA (Fig 1.1)
net("+9V_RC1", ("R501", "2"), ("C502", "1"), ("R502", "1"))
net("+9VA", ("R502", "2"), ("C503", "1"), ("FB2", "1"), ("PF2", "1"))
net("+9VA_OPA", ("FB2", "2"), ("PF6", "1"), ("U2", "8"), ("U3", "8"), ("U4", "8"), ("U6", "8"), ("C521", "1"), ("C522", "1"), ("C523", "1"), ("C524", "1"), ("R504", "1"))
# buck
net("BUCK_SW", ("U5", "5"), ("L1", "1"), ("C510", "1"))
net("BUCK_BST", ("U5", "6"), ("C510", "2"))
net("+5V", ("L1", "2"), ("U5", "1"), ("C508", "1"), ("C509", "1"), ("FB3", "1"), ("FB4", "1"), ("PF3", "1"))
# Seed VIN: bead + 3R3 + 100u (Fig 1.4 alternative)
net("+5V_FB", ("FB3", "2"), ("R503", "1"))
net("+5V_SEED", ("R503", "2"), ("C504", "1"), ("C511", "1"), ("U1", "39"), ("PF4", "1"))
net("+5V_ESP", ("FB4", "2"), ("C505", "1"), ("C512", "1"), ("J5", "1"), ("PF5", "1"))
# AREF
net("AREF_DIV", ("R504", "2"), ("R505", "1"), ("C513", "1"), ("U4", "5"))
net("AREF", ("U4", "7"), ("U4", "6"), ("C514", "1"),
    ("R101", "2"), ("R201", "2"), ("U2", "5"), ("U3", "5"), ("U4", "3"), ("U6", "3"), ("U6", "5"))
# spare follower U6B
net("U6B_OUT", ("U6", "7"), ("U6", "6"))
# GND
gnd = [("J1", "1"), ("J1", "3"), ("D2", "1"), ("C501", "2"), ("C502", "2"), ("C503", "2"), ("C504", "2"), ("C505", "2"), ("C506", "2"),
       ("U5", "4"), ("C507", "2"), ("C508", "2"), ("C509", "2"), ("C511", "2"), ("C512", "2"), ("R505", "2"), ("C513", "2"), ("C514", "2"),
       ("U2", "4"), ("U3", "4"), ("U4", "4"), ("U6", "4"), ("C521", "2"), ("C522", "2"), ("C523", "2"), ("C524", "2"),
       ("U1", "20"), ("U1", "40"), ("J5", "2"), ("J2", "S"), ("J3", "S"), ("SW1", "2"), ("SW2", "2"), ("PF8", "1")]
for n in (1, 2): gnd += [(f"R{n}02", "2"), (f"C{n}02", "2"), (f"C{n}03", "2"), (f"R{n}05", "2"), (f"C{n}06", "2")]
for n in (3, 4): gnd += [(f"R{n}04", "2")]
for i in range(1, 6): gnd += [(f"J1{i}", "2"), (f"D2{i}", "1")]
for i in range(1, 7): gnd += [(f"RV{i}", "3")]
net("GND", *gnd)
# --- audio input channels (Fig 3.4)
def input_nets(n, ch, jackpin, jacknormal, opa, seedpin):
    a, b = f"IN_{ch}", n
    net(f"{a}_TIP", ("J2", jackpin), (f"C{b}01", "2"))          # C01: + faces the 4.5 V bias node
    net(f"{a}_TN", ("J2", jacknormal), (f"R{b}02", "1"))       # tip-normal contact: grounds an unplugged input through 10k
    net(f"{a}_BIAS", (f"C{b}01", "1"), (f"R{b}01", "1"), (f"R{b}03", "1"))
    net(f"{a}_OPIN", (f"R{b}03", "2"), (f"C{b}02", "1"), (opa, "3"))
    net(f"{a}_BUF", (opa, "1"), (opa, "2"), (f"R{b}04", "1"))
    net(f"{a}_DIV", (f"R{b}04", "2"), (f"C{b}03", "1"), (f"R{b}05", "1"), (f"C{b}04", "2"))   # divider node sits at 2.2 V DC
    net(f"{a}_S2IN", (f"C{b}04", "1"), (f"R{b}06", "1"))                                          # virtual ground at 4.5 V: + here
    net(f"{a}_INN", (f"R{b}06", "2"), (opa, "6"), (f"R{b}07", "1"), (f"C{b}05", "1"))
    net(f"{a}_S2OUT", (opa, "7"), (f"R{b}07", "2"), (f"C{b}05", "2"), (f"R{b}08", "1"))
    net(f"{a}_CODEC", (f"R{b}08", "2"), (f"C{b}06", "1"), ("U1", seedpin))
input_nets(1, "L", "T", "TN", "U2", "16")
input_nets(2, "R", "R", "RN", "U3", "17")
net("+3V3A", ("U1", "21"), *[(f"RV{i}", "1") for i in range(1, 7)])
# --- audio outputs (Fig 3.5)
def output_nets(n, ch, opa, seedpin, jackpin):
    a, b = f"OUT_{ch}", n
    net(f"{a}_DAC", ("U1", seedpin), (f"C{b}01", "2"))       # Seed output is AC-coupled internally (0 V DC): - here
    net(f"{a}_C", (f"C{b}01", "1"), (f"R{b}01", "1"))         # summing node at 4.5 V: + here
    net(f"{a}_INN", (f"R{b}01", "2"), (opa, "2"), (f"R{b}02", "1"), (f"C{b}02", "1"))
    net(f"{a}_AMP", (opa, "1"), (f"R{b}02", "2"), (f"C{b}02", "2"), (f"R{b}03", "1"))
    net(f"{a}_RC", (f"R{b}03", "2"), (f"C{b}03", "1"))
    net(f"{a}_TIP", (f"C{b}03", "2"), (f"R{b}04", "1"), ("J3", jackpin))
output_nets(3, "L", "U4", "18", "T")
output_nets(4, "R", "U6", "19", "R")
# --- controls -> Seed3 (pins per firmware/daisy/main.cpp)
for i in range(1, 7): net(f"KNOB{i}", (f"RV{i}", "2"), ("U1", str(21 + i)))        # A0..A5 = pins 22..27
for i in range(1, 6): net(f"FS{i}", (f"J1{i}", "1"), ("U1", str(i)))                # D0..D4 = pins 1..5
net("HP_A", ("SW1", "1"), ("U1", "6")); net("HP_B", ("SW1", "3"), ("U1", "7"))      # D5, D6
net("LP_A", ("SW2", "1"), ("U1", "8")); net("LP_B", ("SW2", "3"), ("U1", "9"))      # D7, D8
for i in range(1, 5): net(f"LED{i}", ("U1", str(9 + i)), (f"R6{i}", "1")); net(f"LED{i}_K", (f"R6{i}", "2"), (f"D2{i}", "2"))   # D9..D12 = pins 10..13
net("LED5", ("U1", "33"), ("R65", "1")); net("LED5_K", ("R65", "2"), ("D25", "2"))                                                 # D26 = pin 33
# UART: Seed D13 (pin 14, TX) -> C3 IO20 (J6.8, RX); Seed D14 (pin 15, RX) <- C3 IO21 (J6.7, TX)
net("UART_SEED_TX", ("U1", "14"), ("J6", "8"))
net("UART_SEED_RX", ("U1", "15"), ("J6", "7"))

# unused pins that ERC must see as intentional no-connects
NO_CONNECT = [("U1", p) for p in ("28", "29", "30", "31", "32", "34", "35", "36", "37", "38")] + \
             [("J5", p) for p in ("3", "4", "5", "6", "7", "8")] + [("J6", p) for p in ("1", "2", "3", "4", "5", "6")] + \
             [("J3", "TN"), ("J3", "RN")]

def pin_to_net():
    m = {}
    for name, conns in N.items():
        for ref, pin in conns:
            assert (ref, pin) not in m, f"{ref}.{pin} in both {m[(ref, pin)]} and {name}"
            m[(ref, pin)] = name
    return m

if __name__ == "__main__":
    m = pin_to_net()
    print(len(P), "parts,", len(N), "nets,", len(m), "connections")
    for k, v in N.items():
        if len(v) < 2: print("single-pin net:", k, v)
