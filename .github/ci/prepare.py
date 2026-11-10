#!/usr/bin/env python3
"""Testbau vorbereiten (GitHub Actions, .github/workflows/firmware.yml): baut genau das, was Nutzer bauen -
esphomeremote.yaml (Remote) und esphomeremote-dock.yaml (Dock) - aber mit dem Stand dieses Commits statt dem Zweig
"stabil": Pakete und external_components zeigen auf die lokalen Dateien. Dazu Platzhalter-Secrets.

    python3 .github/ci/prepare.py <repo> <ziel> [<repo relativ zum ziel>]

Erzeugt in <ziel>: ci-remote.yaml, ci-dock.yaml, secrets.yaml (falls nicht vorhanden) und die *.ci.yaml im Repo.
"""
import re
import sys
from pathlib import Path

repo = Path(sys.argv[1])
ziel = Path(sys.argv[2])
rel = sys.argv[3] if len(sys.argv) > 3 else "."
pre = "" if rel == "." else rel.rstrip("/") + "/"

GIT = re.compile(r"source: \{type: git, url: https://github\.com/Aggro600/ESPHomeRemote, ref: [\w.-]+, path: components\}")


def lokal(text: str, datei: str) -> str:
    neu, n = GIT.subn(f"source: {{type: local, path: {pre}components}}", text)
    if n != 1:
        raise SystemExit(f"{datei}: external_components-Quelle nicht (genau einmal) gefunden")
    return neu


def paket(text: str, datei: str, inhalt: str, name: str) -> str:
    muster = re.compile(r"  " + name + r":\n    url: https://github\.com/Aggro600/ESPHomeRemote\n    ref: [\w.-]+\n"
                        r"    files: \[[^\]]+\]\n    refresh: \w+\n")
    neu, n = muster.subn(f"  {name}: !include {pre}{inhalt}\n", text)
    if n != 1:
        raise SystemExit(f"{datei}: Paket '{name}' von GitHub nicht gefunden")
    return neu


# Remote
fw = (repo / "esphomeremote-firmware.yaml").read_text(encoding="utf-8")
(repo / "esphomeremote-firmware.ci.yaml").write_text(lokal(fw, "esphomeremote-firmware.yaml"), encoding="utf-8")
r = (repo / "esphomeremote.yaml").read_text(encoding="utf-8")
(ziel / "ci-remote.yaml").write_text(paket(r, "esphomeremote.yaml", "esphomeremote-firmware.ci.yaml", "firmware"),
                                     encoding="utf-8")

# Dock (Original-Dock = Kern + Platine)
kern = (repo / "esphomeremote-dock-core.yaml").read_text(encoding="utf-8")
(repo / "esphomeremote-dock-core.ci.yaml").write_text(lokal(kern, "esphomeremote-dock-core.yaml"), encoding="utf-8")
dfw = (repo / "esphomeremote-dock-firmware.yaml").read_text(encoding="utf-8")
dfw, n = re.subn(r"core: !include esphomeremote-dock-core\.yaml", "core: !include esphomeremote-dock-core.ci.yaml", dfw)
if n != 1:
    raise SystemExit("esphomeremote-dock-firmware.yaml: Kern-Paket nicht gefunden")
(repo / "esphomeremote-dock-firmware.ci.yaml").write_text(dfw, encoding="utf-8")
d = (repo / "esphomeremote-dock.yaml").read_text(encoding="utf-8")
(ziel / "ci-dock.yaml").write_text(paket(d, "esphomeremote-dock.yaml", "esphomeremote-dock-firmware.ci.yaml", "basis"),
                                   encoding="utf-8")

# Platzhalter-Secrets (nur zum Bauen)
sec = ziel / "secrets.yaml"
if not sec.exists():
    sec.write_text('wifi_ssid: "ci"\nwifi_password: "ci-password"\nota_pw: "ci-ota-password"\n'
                   'ap_password: "ci-ap-password"\ndirekt_schluessel: "00112233445566778899aabbccddeeff"\n'
                   'esphomeremote_key: "ci-key"\n', encoding="utf-8")
for x in ("ci-remote.yaml", "ci-dock.yaml"):
    t = (ziel / x).read_text(encoding="utf-8")
    if "github.com/Aggro600" in t.split("dashboard_import:")[0] + t.split("import_full_config: false")[-1]:
        raise SystemExit(f"{x}: zeigt noch auf GitHub")
print("Testbau vorbereitet:", ", ".join(str(ziel / x) for x in ("ci-remote.yaml", "ci-dock.yaml")))
