#!/usr/bin/env python3
"""Pakete, die die Tests zusaetzlich zu pytest-homeassistant-custom-component brauchen (2026-11-12).

Home Assistant installiert die Abhaengigkeiten seiner Komponenten erst beim Start - in der Testumgebung gibt es diesen
Schritt nicht. Dieses Skript liest sie aus den manifest.json der Komponenten, von denen die Integration abhaengt, und
gibt sie fuer pip aus:    pip install $(python3 tests/ha_requirements.py)
"""
import json
import pathlib

import homeassistant.components as c

BASIS = pathlib.Path(c.__file__).parent
KOMPONENTEN = ("bluetooth", "usb", "webhook", "websocket_api", "frontend", "panel_custom", "http", "recorder",
               "assist_pipeline", "diagnostics", "repairs")
out: set[str] = set()
for k in KOMPONENTEN:
    m = json.loads((BASIS / k / "manifest.json").read_text())
    out.update(m.get("requirements", []))
print(" ".join(sorted(out)))
