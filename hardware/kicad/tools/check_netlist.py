#!/usr/bin/env python3
"""Gate: KiCad's exported netlist (pedal.xml) must match tools/netlist.py pad-for-pad."""
import sys, os, xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(__file__))
from netlist import pin_to_net, NO_CONNECT
from collections import defaultdict
want = pin_to_net(); got = {}
xml = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "pedal.xml")
for net in ET.parse(xml).getroot().iter('net'):
    for node in net.findall('node'):
        name = net.get('name').lstrip('/')
        if name.startswith('unconnected-'): continue
        got[(node.get('ref'), node.get('pin'))] = name
w = defaultdict(set); g = defaultdict(set)
for k, v in want.items(): w[v].add(k)
for k, v in got.items(): g[v].add(k)
extra = set(got) - set(want); missing = set(want) - set(got)
same = set(map(frozenset, w.values())) == set(map(frozenset, g.values()))
print(f"connections: source {len(want)}, KiCad {len(got)}; extra {sorted(extra)}; missing {sorted(missing)}; partition identical: {same}")
sys.exit(0 if same and not extra and not missing else 1)
