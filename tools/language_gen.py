#!/usr/bin/env python3
"""Uebersetzungstabelle fuer das Display der Remote (2026-11-01, Nutzer: "alles lokalisieren").

Quelle: esphome/language/<code>.json (deutscher Text -> Uebersetzung), aus dem Woerterbuch des Konfigurators plus den
Texten der Firmware. Ausgabe: components/menu/language_table.h - sortiert nach dem deutschen Text (Bytes), die Remote
sucht binaer (sprache.h). Aufruf: python3 tools/language_gen.py   (nach Aenderungen an language/*.json)
"""
import json
from pathlib import Path

E = Path(__file__).resolve().parent.parent


def c(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


wb = json.loads((E / "language" / "en.json").read_text(encoding="utf-8"))
paare = sorted(((k, v) for k, v in wb.items() if v and v != k), key=lambda kv: kv[0].encode())
z = ["// GENERIERT von tools/language_gen.py aus language/en.json - nicht von Hand aendern.", "#pragma once", "",
     "namespace sprache {", "struct Paar { const char *de; const char *en; };",
     f"inline const Paar TAB_EN[] = {{"]
z += [f"  {{{c(k)}, {c(v)}}}," for k, v in paare]
# Teilstuecke fuer zusammengesetzte Texte (Zahlen dazwischen): Indizes langer Eintraege, laengste zuerst
lang = sorted((i for i, (k, v) in enumerate(paare) if len(k.encode()) >= 8), key=lambda i: -len(paare[i][0].encode()))
z += ["};", f"inline const unsigned TAB_EN_N = {len(paare)};",
      "inline const unsigned short TEIL_EN[] = {" + ", ".join(map(str, lang)) + "};",
      f"inline const unsigned TEIL_EN_N = {len(lang)};", "}  // namespace sprache", ""]
(E / "components" / "menu" / "language_table.h").write_text("\n".join(z), encoding="utf-8")
print(len(paare), "Eintraege")
