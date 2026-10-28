import json
E = json.load(open('einst.json'))
TITEL = {'page_bright': ('Bildschirm', 'bright'), 'page_show': ('Anzeige', 'show'), 'page_power': ('Energie & Wecken', 'power'),
         'page_voice_set': ('Sprache', 'voice_set'), 'page_ble': ('Bluetooth', 'ble'), 'page_akku': ('Akku', 'akku'), 'page_extras': ('Smart Display', 'extras'), 'page_service': ('Wartung & Tests', 'service')}
def seiten_id(pg):
    return 'set_' + TITEL[pg][1]
reg = []      # (key, typ, obj, wert, name, seite)
seiten = {}   # pg -> list of items (configurator)
for pg, items in E.items():
    out = []; i = 0
    titel = TITEL[pg][0]
    while i < len(items):
        x = items[i]; k = x['k']
        if k == 'btn' and x['obj'].startswith('back_'): i += 1; continue
        if k == 'txt':
            kopf = i > 0 and items[i - 1]['k'] == 'btn' and items[i - 1]['obj'].startswith('back_')   # Seitentitel
            if x['name'] != titel and not kopf and x['name'] not in ('vor 24 h', 'jetzt'):
                out.append({'t': 'text', 'label': x['name'], 'w': 6, 'h': 1 if len(x['name']) <= 26 else 0})
            i += 1; continue
        if k == 'hint':
            # Erklaerung zur Einstellung davor (Text setzt die Firmware je nach Wert), Hoehe automatisch
            reg.append((x['wert'], 'i', None, x['wert'], 'Hinweis', pg))
            out.append({'t': 'setting', 'src': x['wert'], 'label': '', 'w': 6, 'h': 0}); i += 1; continue
        if k == 'hdr':
            # Ueberschriften GROSS -> normal; Namen mit festen Grossbuchstaben aus KOPF (2026-10-17)
            KOPF = {'MAUS & TASTATUR JE GERÄT': 'Maus & Tastatur je Gerät', 'SMART DISPLAY': 'Smart Display', 'MAUS (TOUCHPAD)': 'Maus (Touchpad)'}
            if x['name'].strip(): out.append({'t': 'text', 'label': KOPF.get(x['name']) or (x['name'][:1] + x['name'][1:].lower() if x['name'].isupper() and len(x['name']) > 4 else x['name']), 'w': 6, 'h': 1})
            i += 1; continue
        if k == 'pair' and i + 1 < len(items) and items[i + 1]['k'] == 'sld':
            s = items[i + 1]
            reg.append((s['obj'], 's', s['obj'], x['wert'], x['name'], pg))
            out.append({'t': 'setting', 'src': s['obj'], 'label': x['name'], 'w': 6, 'h': 2})
            i += 2; continue
        if k == 'pair':
            reg.append((x['wert'], 'i', None, x['wert'], x['name'], pg))
            out.append({'t': 'setting', 'src': x['wert'], 'label': x['name'], 'w': 6, 'h': 1}); i += 1; continue
        if k == 'lbl':
            if x['wert'] in ('ak_graph_leer',): i += 1; continue
            reg.append((x['wert'], 'i', None, x['wert'], x['text'] or x['wert'], pg))
            out.append({'t': 'setting', 'src': x['wert'], 'label': '', 'w': 6, 'h': 1}); i += 1; continue
        if k == 'btn':
            name = x['name']
            if x['obj'] == 'tile_mic': name = 'Mikrofon'
            if x['obj'].startswith('ble_slot'): name = name.split('  ', 1)[-1]
            reg.append((x['obj'], 'b', x['obj'], x['wert'], name, pg))
            w = 2 if x['obj'].startswith('mm_') or x['obj'] in ('ble_connect', 'ble_pair', 'ble_forget') else 6   # drei Aktionen nebeneinander
            # Bluetooth-Geraete zweizeilig (2026-10-06): Name + Status passten nicht in eine Zeile ("Google Streamer...")
            h = 2 if x['obj'].startswith('ble_slot') else 1
            out.append({'t': 'setting', 'src': x['obj'], 'label': name, 'w': w, 'h': h}); i += 1; continue
        i += 1
    seiten[pg] = out
# Akku: Verlauf-Grafik direkt im Menue (2026-10-07; vorher eigene Seite "Verlauf (Grafik)" = alles doppelt)
reg.append(('ak_graph', 'g', 'ak_graph->obj', None, 'Verlauf 24 h', 'page_akku'))
_ak = seiten['page_akku']
_i = next(i for i, x in enumerate(_ak) if x.get('t') == 'text' and x['label'].lower().startswith('verlauf'))
_ak.insert(_i + 1, {'t': 'setting', 'src': 'ak_graph', 'label': 'Verlauf 24 h', 'w': 6, 'h': 2})
# --- C++-Tabelle
h = ['// GENERIERT (tools/einst_parse.py + einst_gen.py): Einstellungs-Eintraege des Konfigurator-Menues -> Widgets der',
     '// eingebauten Einstellungsseiten. Das Menue steuert diese Widgets fern (Klick / Regler), die Logik bleibt in der YAML.',
     '#pragma once', '#include "esphome/components/menue/menue.h"', '', 'namespace einstellungen {', '',
     'inline esphome::menue::EinstRef finden(const std::string &k) {',
     '  struct Z { const char *k; char t; lv_obj_t *o; lv_obj_t *w; uint8_t g; };',
     '  static const Z tab[] = {   // static: erst beim ersten Aufruf (nach dem LVGL-Aufbau) gefuellt']
import re as _re
_yaml = open('/config/esphome/remote-wz-couch.yaml').read()
# Gruppe = Bit des Auffrisch-Skripts der eingebauten Seite (2026-10-06: das Menue frischt nur die Gruppen
# seiner Eintraege auf - vorher liefen bei JEDER Einstellungsseite alle Skripte, ~120 ms bei Energie & Wecken)
_skripte = [pg for pg in E if _re.search(rf'^  - id: einst_{pg[5:]}$', _yaml, _re.M)]
for key, t, o, w, name, pg in reg:
    g = _skripte.index(pg) if pg in _skripte else 31
    h.append(f'    {{"{key}", \'{t}\', {o or "nullptr"}, {w or "nullptr"}, {g}}},   // {pg[5:]}: {name}')
h += ['  };', '  for (const auto &z : tab) if (k == z.k) return {z.t, z.o, z.w, z.g};', '  return {};', '}', '',
      '// Werte der eingebauten Seiten auffrischen (ohne Seitenwechsel); maske = Bits der Gruppen (EinstRef::gruppe)',
      'inline void auffrischen(uint32_t maske = 0xFFFFFFFFu) {']
for i, pg in enumerate(_skripte):
    h.append(f'  if (maske & (1u << {i})) einst_{pg[5:]}->execute();')
h += ['}', '', '}  // namespace einstellungen', '']
open('/config/esphome/esphomeremote-einstellungen.h', 'w').write('\n'.join(h))
import pprint
_s = [{"id": seiten_id(pg), "title": TITEL[pg][0], "remember": True, "flow": True, "items": it} for pg, it in seiten.items()]
open('/config/custom_components/esphomeremote_konfig/einstellungen.py', 'w').write(
    '"""GENERIERT (esphome/tools/einst_gen.py): Einstellungsseiten der Remote als Konfigurator-Seiten.\n'
    'Eintraege t="setting" steuern die Widgets der eingebauten Seiten fern (esphomeremote-einstellungen.h)."""\n\n'
    'EINST_SEITEN = ' + pprint.pformat(_s, width=140, sort_dicts=False) + '\n')
_reg = [[k, t, (n + ' (' + k + ')') if n == 'Hinweis' else n, TITEL[pg][0]] for k, t, o, w, n, pg in reg]
# Liste fuers Panel direkt in den Konfigurator schreiben (const M_EINST = [...];)
import re as _re2
_js = '/config/custom_components/esphomeremote_konfig/frontend/konfigurator.js'
_t = open(_js).read()
_t = _re2.sub(r'^const M_EINST = \[.*?\];$', lambda m: 'const M_EINST = ' + json.dumps(_reg, ensure_ascii=False) + ';', _t, count=1, flags=_re2.M)
open(_js, 'w').write(_t)
print(len(reg), 'Einstellungen;', {TITEL[pg][1]: len(it) for pg, it in seiten.items()})
