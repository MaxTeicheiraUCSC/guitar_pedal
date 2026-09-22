"""Minimal s-expression reader for KiCad files (lists of str/float/list)."""
import re
_tok = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))', re.S)
def parse(text):
    pos = 0; stack = [[]]
    while True:
        m = _tok.match(text, pos)
        if not m: break
        pos = m.end()
        if m.group(1): stack.append([])
        elif m.group(2): top = stack.pop(); stack[-1].append(top)
        elif m.group(3): stack[-1].append(m.group(3)[1:-1].replace('\\"','"'))
        elif m.group(4): stack[-1].append(m.group(4))
        if pos >= len(text): break
    return stack[0]
def find_all(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]
def find(node, key):
    r = find_all(node, key); return r[0] if r else None
def block_text(text, start):
    """Raw text of the top-level s-expression beginning at `start` (for verbatim copies)."""
    i = text.find(start); assert i >= 0, start
    d = 0; j = i; instr = False
    while True:
        c = text[j]
        if c == '"' and text[j-1] != '\\': instr = not instr
        elif not instr:
            if c == '(': d += 1
            elif c == ')':
                d -= 1
                if d == 0: return text[i:j+1]
        j += 1
def symbol_pins(libtext, name):
    """[(number, name, electrical_type, x, y, rot)] for symbol `name` (follows `extends`)."""
    doc = parse(libtext)[0]   # (kicad_symbol_lib ...)
    syms = {s[1]: s for s in find_all(doc, 'symbol')}
    s = syms[name]
    ext = find(s, 'extends')
    chain = [s] + ([syms[ext[1]]] if ext else [])
    out = []
    for sym in chain:
        for unit in find_all(sym, 'symbol'):
            for p in find_all(unit, 'pin'):
                at = find(p, 'at'); nm = find(p, 'name'); num = find(p, 'number')
                out.append((num[1], nm[1], p[1], float(at[1]), float(at[2]), int(float(at[3]))))
    return out
