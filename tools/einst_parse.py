import re, json
Y = open('/config/esphome/remote-wz-couch.yaml').read().split('\n')
PAGES = ['page_bright','page_show','page_power','page_voice_set','page_ble','page_akku','page_extras','page_service']
def section(pg):
    s = next(i for i,l in enumerate(Y) if l == f'    - id: {pg}')
    e = next(i for i in range(s+1, len(Y)) if re.match(r'    - id: page_', Y[i]))
    return Y[s:e]
def ind(l): return len(l) - len(l.lstrip())
out = {}
for pg in PAGES:
    L = section(pg); items = []; i = 0
    # Inhalte erst nach der Kopfzeile (hdr_back)
    while i < len(L):
        l = L[i]; st = l.strip()
        m = re.match(r'- (button|slider|label|obj):\s*(\{.*\})?$', st)
        if not m: i += 1; continue
        kind, inline = m.group(1), m.group(2)
        n0 = ind(l)
        j = i + 1
        while j < len(L) and (L[j].strip() == '' or ind(L[j]) > n0): j += 1
        block = L[i:j]; btxt = '\n'.join(block)
        idm = re.search(r'\bid: ([a-z0-9_]+)', block[0] if inline else '\n'.join(block[:3]))
        oid = idm.group(1) if idm else None
        labels = re.findall(r'label: \{([^}]*)\}', btxt)
        def lab(x):
            t = re.search(r'text: "([^"]*)"', x); d = re.search(r'id: ([a-z0-9_]+)', x)
            return (t.group(1) if t else None, d.group(1) if d else None, '0x3B82F6' in x)
        if kind == 'button' and oid:
            ls = [lab(x) for x in labels]
            name = next((t for t, d, _ in ls if t and not t.startswith('\\U') and t.strip()), None)
            wert = next((d for t, d, _ in ls if d and d != oid), None)
            items.append({'k': 'btn', 'obj': oid, 'name': name, 'wert': wert}); i = j; continue
        if kind == 'slider' and oid:
            items.append({'k': 'sld', 'obj': oid}); i = j; continue
        if kind == 'obj' and not oid:
            ls = [lab(x) for x in labels]
            if len(ls) == 2 and ls[0][0] and ls[1][1] and 'button' not in btxt and 'slider' not in btxt:
                items.append({'k': 'pair', 'name': ls[0][0], 'wert': ls[1][1]}); i = j; continue
            i += 1; continue
        if kind == 'label' and inline is not None:
            t, d, blau = lab(inline)
            if blau and t: items.append({'k': 'hdr', 'name': t})
            elif t and d is None and t.strip() and not t.startswith('\\U'): items.append({'k': 'txt', 'name': t})
            elif d and (d.startswith(('h_', 'ps_h_')) or d in ('ps_timeline', 'ble_kbd_hint')): items.append({'k': 'hint', 'wert': d})
            elif d: items.append({'k': 'lbl', 'wert': d, 'text': t})
            i = j; continue
        i += 1
    out[pg] = items
json.dump(out, open('einst.json', 'w'), ensure_ascii=False, indent=0)
for pg, it in out.items():
    print('==', pg)
    for x in it: print('  ', x)
